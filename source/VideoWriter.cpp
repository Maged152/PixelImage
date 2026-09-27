#include "video.hpp"
#include "minih264/minih264e.h"
#include "minimp4/minimp4.h"
#include "stb/stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace qlm
{
	namespace
	{
		// ---------------------------------------------------------------------------------
		// MP4 (ISO/IEC 14496-12) box writers.
		//
		// Only the boxes needed by a single Motion-JPEG video track are written. Because
		// every sample is an independent image, the sample table stays trivial: one chunk
		// holding all samples, a constant sample duration and no stss box (the absence of
		// stss means that every sample is a sync sample).
		// All integer fields in an MP4 file are big-endian.
		// ---------------------------------------------------------------------------------

		void AppendU8(std::vector<uint8_t>& out, uint8_t value)
		{
			out.push_back(value);
		}

		void AppendU16(std::vector<uint8_t>& out, uint16_t value)
		{
			out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
			out.push_back(static_cast<uint8_t>(value & 0xFF));
		}

		void AppendU32(std::vector<uint8_t>& out, uint32_t value)
		{
			out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
			out.push_back(static_cast<uint8_t>(value & 0xFF));
		}

		void AppendU64(std::vector<uint8_t>& out, uint64_t value)
		{
			AppendU32(out, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu));
			AppendU32(out, static_cast<uint32_t>(value & 0xFFFFFFFFu));
		}

		void AppendBytes(std::vector<uint8_t>& out, const void* bytes, size_t size)
		{
			const uint8_t* first = static_cast<const uint8_t*>(bytes);
			out.insert(out.end(), first, first + size);
		}

		void AppendZeros(std::vector<uint8_t>& out, size_t count)
		{
			out.insert(out.end(), count, 0);
		}

		// Appends a box header and returns the position of its size field.
		size_t BeginBox(std::vector<uint8_t>& out, const char* type)
		{
			const size_t size_position = out.size();
			AppendU32(out, 0); // filled in by EndBox
			AppendBytes(out, type, 4);
			return size_position;
		}

		// As BeginBox, for boxes that carry a version and flags field.
		size_t BeginFullBox(std::vector<uint8_t>& out, const char* type, uint8_t version, uint32_t flags)
		{
			const size_t size_position = BeginBox(out, type);
			AppendU8(out, version);
			AppendU8(out, static_cast<uint8_t>((flags >> 16) & 0xFF));
			AppendU8(out, static_cast<uint8_t>((flags >> 8) & 0xFF));
			AppendU8(out, static_cast<uint8_t>(flags & 0xFF));
			return size_position;
		}

		void EndBox(std::vector<uint8_t>& out, size_t size_position)
		{
			const uint32_t size = static_cast<uint32_t>(out.size() - size_position);
			out[size_position] = static_cast<uint8_t>((size >> 24) & 0xFF);
			out[size_position + 1] = static_cast<uint8_t>((size >> 16) & 0xFF);
			out[size_position + 2] = static_cast<uint8_t>((size >> 8) & 0xFF);
			out[size_position + 3] = static_cast<uint8_t>(size & 0xFF);
		}

		// The unity matrix, stored in 16.16 / 2.30 fixed point.
		void AppendUnityMatrix(std::vector<uint8_t>& out)
		{
			AppendU32(out, 0x00010000); AppendU32(out, 0x00000000); AppendU32(out, 0x00000000);
			AppendU32(out, 0x00000000); AppendU32(out, 0x00010000); AppendU32(out, 0x00000000);
			AppendU32(out, 0x00000000); AppendU32(out, 0x00000000); AppendU32(out, 0x40000000);
		}

		// -----------------------------------------------------------------------------
		// stdio helpers that survive files larger than 2 GB.
		// -----------------------------------------------------------------------------

		int64_t FilePosition(std::FILE* file)
		{
		#if defined(_MSC_VER) || defined(__MINGW32__)
			return static_cast<int64_t>(_ftelli64(file));
		#else
			return static_cast<int64_t>(std::ftell(file));
		#endif
		}

		bool FileSeekTo(std::FILE* file, int64_t position)
		{
		#if defined(_MSC_VER) || defined(__MINGW32__)
			return _fseeki64(file, position, SEEK_SET) == 0;
		#else
			return std::fseek(file, static_cast<long>(position), SEEK_SET) == 0;
		#endif
		}

		std::FILE* OpenBinaryForWriting(const std::string& file_name)
		{
		#if defined(_MSC_VER)
			std::FILE* file = nullptr;
			if (fopen_s(&file, file_name.c_str(), "wb+") != 0)
				return nullptr;
			return file;
		#else
			return std::fopen(file_name.c_str(), "wb+");
		#endif
		}

		// Everything the index (moov) needs to know about the media data.
		struct MoovInfo
		{
			const std::vector<uint32_t>* sample_sizes = nullptr;
			int width = 0;
			int height = 0;
			int frame_rate = 0;
			uint64_t chunk_offset = 0; // file position of the first sample byte
		};

		// Builds the whole moov box. The media timescale is frame_rate * 1000 so that an
		// integer frame rate is represented exactly with a constant sample duration.
		std::vector<uint8_t> BuildMoov(const MoovInfo& info)
		{
			std::vector<uint8_t> moov;

			const uint32_t frame_count = static_cast<uint32_t>(info.sample_sizes->size());
			const uint32_t media_timescale = static_cast<uint32_t>(info.frame_rate) * 1000u;
			const uint32_t sample_delta = 1000u;
			const uint64_t media_duration = static_cast<uint64_t>(frame_count) * sample_delta;

			const uint32_t movie_timescale = 1000u;
			const uint32_t movie_duration = info.frame_rate > 0
				? static_cast<uint32_t>((static_cast<uint64_t>(frame_count) * movie_timescale)
										/ static_cast<uint32_t>(info.frame_rate))
				: 0u;

			// 'stco' holds 32-bit offsets, 'co64' 64-bit ones
			const bool large_offsets = info.chunk_offset > 0xFFFFFFFFull;
			const char handler_name[] = "VideoHandler";

			const size_t moov_position = BeginBox(moov, "moov");
			{
				// Movie header
				const size_t mvhd = BeginFullBox(moov, "mvhd", 0, 0);
				AppendU32(moov, 0);                         // creation_time
				AppendU32(moov, 0);                         // modification_time
				AppendU32(moov, movie_timescale);
				AppendU32(moov, movie_duration);
				AppendU32(moov, 0x00010000);                // rate 1.0
				AppendU16(moov, 0x0100);                    // volume 1.0
				AppendU16(moov, 0);                         // reserved
				AppendZeros(moov, 8);                       // reserved
				AppendUnityMatrix(moov);
				AppendZeros(moov, 24);                      // pre_defined
				AppendU32(moov, 2);                         // next_track_ID
				EndBox(moov, mvhd);

				const size_t trak = BeginBox(moov, "trak");
				{
					// Track header, flags: enabled | in movie | in preview
					const size_t tkhd = BeginFullBox(moov, "tkhd", 0, 7);
					AppendU32(moov, 0);                     // creation_time
					AppendU32(moov, 0);                     // modification_time
					AppendU32(moov, 1);                     // track_ID
					AppendU32(moov, 0);                     // reserved
					AppendU32(moov, movie_duration);
					AppendZeros(moov, 8);                   // reserved
					AppendU16(moov, 0);                     // layer
					AppendU16(moov, 0);                     // alternate_group
					AppendU16(moov, 0);                     // volume, unused for video
					AppendU16(moov, 0);                     // reserved
					AppendUnityMatrix(moov);
					AppendU32(moov, static_cast<uint32_t>(info.width) << 16);  // 16.16 fixed point
					AppendU32(moov, static_cast<uint32_t>(info.height) << 16);
					EndBox(moov, tkhd);

					const size_t mdia = BeginBox(moov, "mdia");
					{
						// Media header, language 0x55C4 is "und"
						const size_t mdhd = BeginFullBox(moov, "mdhd", 0, 0);
						AppendU32(moov, 0);                 // creation_time
						AppendU32(moov, 0);                 // modification_time
						AppendU32(moov, media_timescale);
						AppendU32(moov, static_cast<uint32_t>(media_duration));
						AppendU16(moov, 0x55C4);            // language
						AppendU16(moov, 0);                 // pre_defined
						EndBox(moov, mdhd);

						// Handler, marks the media as video
						const size_t hdlr = BeginFullBox(moov, "hdlr", 0, 0);
						AppendU32(moov, 0);                 // pre_defined
						AppendBytes(moov, "vide", 4);       // handler_type
						AppendZeros(moov, 12);              // reserved
						AppendBytes(moov, handler_name, sizeof(handler_name));
						EndBox(moov, hdlr);

						const size_t minf = BeginBox(moov, "minf");
						{
							// Video media header, flags 1 means the opcolor is used
							const size_t vmhd = BeginFullBox(moov, "vmhd", 0, 1);
							AppendU16(moov, 0);             // graphicsmode
							AppendZeros(moov, 6);           // opcolor
							EndBox(moov, vmhd);

							// Data information, one self contained data reference
							const size_t dinf = BeginBox(moov, "dinf");
							{
								const size_t dref = BeginFullBox(moov, "dref", 0, 0);
								AppendU32(moov, 1);         // entry_count
								const size_t url = BeginFullBox(moov, "url ", 0, 1);
								EndBox(moov, url);
								EndBox(moov, dref);
							}
							EndBox(moov, dinf);

							// Sample table
							const size_t stbl = BeginBox(moov, "stbl");
							{
								// Sample description: a QuickTime Motion-JPEG ('jpeg') entry.
								// No codec specific child box is needed because every sample
								// is a self contained JPEG image.
								const size_t stsd = BeginFullBox(moov, "stsd", 0, 0);
								AppendU32(moov, 1);         // entry_count
								{
									const size_t entry = BeginBox(moov, "jpeg");
									AppendZeros(moov, 6);                       // reserved
									AppendU16(moov, 1);                         // data_reference_index
									AppendU16(moov, 0);                         // pre_defined
									AppendU16(moov, 0);                         // reserved
									AppendZeros(moov, 12);                      // pre_defined
									AppendU16(moov, static_cast<uint16_t>(info.width));
									AppendU16(moov, static_cast<uint16_t>(info.height));
									AppendU32(moov, 0x00480000);                // horizresolution, 72 dpi
									AppendU32(moov, 0x00480000);                // vertresolution, 72 dpi
									AppendU32(moov, 0);                         // reserved
									AppendU16(moov, 1);                         // frame_count
									AppendZeros(moov, 32);                      // compressorname
									AppendU16(moov, 0x0018);                    // depth
									AppendU16(moov, 0xFFFF);                    // pre_defined
									EndBox(moov, entry);
								}
								EndBox(moov, stsd);

								// Decoding time to sample: one entry, a constant frame duration
								const size_t stts = BeginFullBox(moov, "stts", 0, 0);
								AppendU32(moov, frame_count > 0 ? 1u : 0u);
								if (frame_count > 0)
								{
									AppendU32(moov, frame_count);
									AppendU32(moov, sample_delta);
								}
								EndBox(moov, stts);

								// Sample to chunk: every sample lives in the single chunk
								const size_t stsc = BeginFullBox(moov, "stsc", 0, 0);
								AppendU32(moov, frame_count > 0 ? 1u : 0u);
								if (frame_count > 0)
								{
									AppendU32(moov, 1);             // first_chunk
									AppendU32(moov, frame_count);   // samples_per_chunk
									AppendU32(moov, 1);             // sample_description_index
								}
								EndBox(moov, stsc);

								// Sample sizes: sample_size is 0, so one size per sample follows
								const size_t stsz = BeginFullBox(moov, "stsz", 0, 0);
								AppendU32(moov, 0);
								AppendU32(moov, frame_count);
								for (const uint32_t size : *info.sample_sizes)
									AppendU32(moov, size);
								EndBox(moov, stsz);

								// Chunk offset, 32-bit or 64-bit depending on the file size
								const size_t stco = BeginFullBox(moov, large_offsets ? "co64" : "stco", 0, 0);
								AppendU32(moov, frame_count > 0 ? 1u : 0u);
								if (frame_count > 0)
								{
									if (large_offsets)
										AppendU64(moov, info.chunk_offset);
									else
										AppendU32(moov, static_cast<uint32_t>(info.chunk_offset));
								}
								EndBox(moov, stco);

								// No stss box: without it every sample is a sync sample
							}
							EndBox(moov, stbl);
						}
						EndBox(moov, minf);
					}
					EndBox(moov, mdia);
				}
				EndBox(moov, trak);
			}
			EndBox(moov, moov_position);

			return moov;
		}

		// stb_image_write callback that appends the encoded JPEG to a vector.
		void AppendJpegData(void* context, void* data, int size)
		{
			std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			out->insert(out->end(), bytes, bytes + size);
		}
		// ---------------------------------------------------------------------------------
		// H.264 writing (VideoFormat::MP4_H264).
		//
		// minih264 encodes one frame at a time and reports every NAL unit of that frame
		// through a callback. The callback forwards the NAL units, start code included, to
		// minimp4's H.264 multiplexer, which collects the SPS/PPS into an avcC box, marks
		// the IDR frames as sync samples (stss) and builds the sample table. The track
		// timescale is 90000, so the duration of a frame is exact for the whole-number
		// frame rates the writer accepts (90000 / frame_rate).
		// ---------------------------------------------------------------------------------
		struct H264State
		{
			MP4E_mux_t* mux = nullptr;
			mp4_h26x_writer_t writer{};
			H264E_persist_t* encoder = nullptr;
			H264E_scratch_t* scratch = nullptr;
			std::vector<uint8_t> i420;    // frame in the planar 4:2:0 layout the encoder wants
			int frame_ticks = 0;          // duration of one frame, in 90 kHz units
			bool failed = false;          // set when a NAL unit is rejected
		};

		// minih264 reports a NAL unit without its start code, so the first byte the
		// multiplexer has to see lies four bytes before the reported one. See nal_start()
		// and nal_end() in minih264e.h.
		const int start_code_bytes = 4;

		void AppendNalu(const unsigned char* nalu_data, int sizeof_nalu_data, void* token)
		{
			H264State* state = static_cast<H264State*>(token);

			if (state->failed)
				return;

			const unsigned char* nal = nalu_data - start_code_bytes;

			// The encoder writes the start code itself, only the payload is reported
			if (nal[0] != 0 || nal[1] != 0 || nal[2] != 0 || nal[3] != 1)
			{
				state->failed = true;
				return;
			}

			if (mp4_h26x_write_nal(&state->writer, nal, sizeof_nalu_data + start_code_bytes,
								   state->frame_ticks) != MP4E_STATUS_OK)
				state->failed = true;
		}

		// minimp4 writes through this callback. The offsets are absolute and the mdat size
		// is patched when the file is closed, so the file stays seekable until then.
		int WriteMp4Block(int64_t offset, const void* buffer, size_t size, void* token)
		{
			std::FILE* file = static_cast<std::FILE*>(token);

			if (!FileSeekTo(file, offset) || std::fwrite(buffer, 1, size, file) != size)
				return MP4E_STATUS_FILE_WRITE_ERROR;

			return MP4E_STATUS_OK;
		}

		// The encoder wants 64 byte aligned persistent and scratch buffers. The raw pointer
		// is handed back as well, because only it can be released again.
		uint8_t* AllocAligned(const size_t size, uint8_t** raw)
		{
			uint8_t* base = new uint8_t[size + 64];
			*raw = base;

			const size_t misaligned = static_cast<size_t>(reinterpret_cast<uintptr_t>(base) & 63u);
			const size_t offset = misaligned == 0 ? 0 : 64 - misaligned;

			return base + offset;
		}

		// quality grows with the image quality, the quantizer shrinks with it: 10 is close
		// to lossless, 51 is very rough. The linear map keeps both formats comparable.
		int QualityToQuantizer(const int quality)
		{
			return std::clamp(51 - (quality * 41) / 100, 10, 51);
		}

		// BT.601 luma and chroma, the integer approximations the JPEG standard uses too.
		uint8_t Luma(const uint8_t r, const uint8_t g, const uint8_t b)
		{
			return static_cast<uint8_t>(((66 * r + 129 * g + 25 * b) >> 8) + 16);
		}

		uint8_t ChromaU(const uint8_t r, const uint8_t g, const uint8_t b)
		{
			return static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b) >> 8) + 128);
		}

		uint8_t ChromaV(const uint8_t r, const uint8_t g, const uint8_t b)
		{
			return static_cast<uint8_t>(((112 * r - 94 * g - 18 * b) >> 8) + 128);
		}
	}

	// The file layout is
	//     ftyp | mdat (every JPEG sample) | moov (the index)
	// which is the same order FFmpeg writes without the +faststart option. The mdat size
	// field is patched, and the index appended, when the file is closed.
	struct VideoWriter::Impl
	{
		std::FILE* file = nullptr;
		std::string file_name;
		int width = 0;
		int height = 0;
		int frame_rate = 0;
		int quality = 90;
		int64_t mdat_size_position = 0;      // position of the mdat box size field
		uint64_t mdat_payload_position = 0;  // position of the first sample byte
		uint64_t mdat_payload_size = 0;
		std::vector<uint32_t> sample_sizes;
		std::vector<uint8_t> packed_pixels;
		std::vector<uint8_t> jpeg;

		// H.264 path
		VideoFormat format = VideoFormat::MP4_MJPEG;
		H264State h264;
		uint8_t* h264_encoder_storage = nullptr;  // raw allocations behind h264.encoder
		uint8_t* h264_scratch_storage = nullptr;  // raw allocations behind h264.scratch
		int h264_quantizer = 33;

		// Number of frames accepted by WriteFrame, both formats
		int frames_written = 0;
	};

	VideoWriter::VideoWriter() : impl(std::make_unique<Impl>())
	{
	}

	VideoWriter::~VideoWriter()
	{
		Close();
	}

	VideoWriter::VideoWriter(VideoWriter&& other) noexcept = default;

	VideoWriter& VideoWriter::operator=(VideoWriter&& other) noexcept
	{
		if (this != &other)
		{
			// The file this object has been writing must be finished before its handle is
			// dropped, otherwise the index (moov) would never be written and the file
			// would be left without one.
			Close();
			impl = std::move(other.impl);
		}

		return *this;
	}

	bool VideoWriter::Open(const std::string& file_name, int frame_width, int frame_height, int frame_rate,
						   int quality, VideoFormat format)
	{
		Close();

		if (impl == nullptr) // this object was moved from, so it owns nothing yet
			impl = std::make_unique<Impl>();

		if (format != VideoFormat::MP4_MJPEG && format != VideoFormat::MP4_H264)
		{
			std::cerr << "Error: unsupported video format." << std::endl;
			return false;
		}

		if (frame_width <= 0 || frame_height <= 0 || frame_width > 0xFFFF || frame_height > 0xFFFF)
		{
			std::cerr << "Error: invalid video size " << frame_width << "x" << frame_height
					  << ", each side must be between 1 and 65535." << std::endl;
			return false;
		}

		// The H.264 encoder works on 16 x 16 macroblocks, so a frame has to fill a whole
		// number of them. Motion-JPEG has no such rule.
		if (format == VideoFormat::MP4_H264 && (frame_width % 16 != 0 || frame_height % 16 != 0))
		{
			std::cerr << "Error: H.264 needs a frame size that is a multiple of 16, but "
					  << frame_width << "x" << frame_height << " was requested." << std::endl;
			return false;
		}

		if (frame_rate <= 0 || frame_rate > 90000)
		{
			std::cerr << "Error: invalid frame rate " << frame_rate << "." << std::endl;
			return false;
		}

		impl->file = OpenBinaryForWriting(file_name);
		if (impl->file == nullptr)
		{
			std::cerr << "Error: cannot open the video file " << file_name << " for writing." << std::endl;
			return false;
		}

		impl->file_name = file_name;
		impl->width = frame_width;
		impl->height = frame_height;
		impl->frame_rate = frame_rate;
		impl->quality = std::clamp(quality, 1, 100);
		impl->format = format;
		impl->frames_written = 0;
		impl->sample_sizes.clear();
		impl->mdat_payload_size = 0;

		if (format == VideoFormat::MP4_H264)
		{
			// The container is written by the vendored minimp4 multiplexer: it writes the
			// file type itself, streams the samples to disk as they are encoded and appends
			// the index when the file is closed. A frame duration of 90000 / frame_rate
			// keeps whole frame rates exact.
			impl->h264.mux = MP4E_open(0, 0, impl->file, &WriteMp4Block);
			if (impl->h264.mux == nullptr)
			{
				std::cerr << "Error: cannot create the MP4 multiplexer for " << file_name << "." << std::endl;
				Close();
				return false;
			}

			if (mp4_h26x_write_init(&impl->h264.writer, impl->h264.mux, frame_width, frame_height, 0) != MP4E_STATUS_OK)
			{
				std::cerr << "Error: cannot add the H.264 video track to " << file_name << "." << std::endl;
				Close();
				return false;
			}

			H264E_create_param_t create;
			std::memset(&create, 0, sizeof(create));

			create.width = frame_width;
			create.height = frame_height;
			create.gop = frame_rate;  // a key frame every second, the first frame is one too
#if H264E_SVC_API
			create.num_layers = 1;    // one AVC layer, no spatial scalability
#endif
#if defined(__ARM_NEON) || defined(_M_ARM64)
			create.enableNEON = 1;
#endif

			int persist_bytes = 0;
			int scratch_bytes = 0;

			if (H264E_sizeof(&create, &persist_bytes, &scratch_bytes) != H264E_STATUS_SUCCESS ||
				persist_bytes <= 0 || scratch_bytes <= 0)
			{
				std::cerr << "Error: cannot size the H.264 encoder for " << frame_width << "x"
						  << frame_height << "." << std::endl;
				Close();
				return false;
			}

			impl->h264.encoder = reinterpret_cast<H264E_persist_t*>(
				AllocAligned(static_cast<size_t>(persist_bytes), &impl->h264_encoder_storage));
			impl->h264.scratch = reinterpret_cast<H264E_scratch_t*>(
				AllocAligned(static_cast<size_t>(scratch_bytes), &impl->h264_scratch_storage));

			if (H264E_init(impl->h264.encoder, &create) != H264E_STATUS_SUCCESS)
			{
				std::cerr << "Error: cannot start the H.264 encoder for " << frame_width << "x"
						  << frame_height << "." << std::endl;
				Close();
				return false;
			}

			impl->h264.i420.resize(static_cast<size_t>(frame_width) * frame_height * 3 / 2);
			impl->h264.frame_ticks = 90000 / frame_rate;
			impl->h264_quantizer = QualityToQuantizer(impl->quality);

			return true;
		}

		// Motion-JPEG: file type, then the media data header; the mdat size is patched by
		// Close and the index appended after the samples.
		std::vector<uint8_t> header;
		const size_t ftyp = BeginBox(header, "ftyp");
		AppendBytes(header, "isom", 4);     // major_brand
		AppendU32(header, 0x00000200);      // minor_version
		AppendBytes(header, "isom", 4);     // compatible_brands
		AppendBytes(header, "iso2", 4);
		AppendBytes(header, "mp41", 4);
		EndBox(header, ftyp);

		impl->mdat_size_position = static_cast<int64_t>(header.size());
		AppendU32(header, 0);               // patched by Close
		AppendBytes(header, "mdat", 4);

		if (std::fwrite(header.data(), 1, header.size(), impl->file) != header.size())
		{
			std::cerr << "Error: cannot write to the video file " << file_name << "." << std::endl;
			std::fclose(impl->file);
			impl->file = nullptr;
			return false;
		}

		// The samples follow the header directly, so the payload position is known here.
		impl->mdat_payload_position = static_cast<uint64_t>(FilePosition(impl->file));

		return true;
	}

	void VideoWriter::Close()
	{
		if (impl == nullptr || impl->file == nullptr)
			return;

		if (impl->format == VideoFormat::MP4_H264)
		{
			// The H.264 multiplexer owns the layout of the file: closing it writes the
			// index, exactly like the hand written Motion-JPEG index below.
			mp4_h26x_write_close(&impl->h264.writer); // also clears h264.writer.mux

			if (impl->h264.mux != nullptr)
			{
				if (MP4E_close(impl->h264.mux) != MP4E_STATUS_OK)
					std::cerr << "Error: failed to finish writing the video file " << impl->file_name << "." << std::endl;

				impl->h264.mux = nullptr;
			}

			// The encoder is only needed while the file is open
			delete[] impl->h264_encoder_storage;
			delete[] impl->h264_scratch_storage;
			impl->h264_encoder_storage = nullptr;
			impl->h264_scratch_storage = nullptr;
			impl->h264.encoder = nullptr;
			impl->h264.scratch = nullptr;
			impl->h264.i420.clear();
			impl->h264.failed = false;

			std::fclose(impl->file);
			impl->file = nullptr;
			return;
		}

		// Patch the media data size, then append the index.
		const uint64_t mdat_size = impl->mdat_payload_size + 8;
		const uint8_t mdat_size_bytes[4] =
		{
			static_cast<uint8_t>((mdat_size >> 24) & 0xFF),
			static_cast<uint8_t>((mdat_size >> 16) & 0xFF),
			static_cast<uint8_t>((mdat_size >> 8) & 0xFF),
			static_cast<uint8_t>(mdat_size & 0xFF)
		};

		bool ok = FileSeekTo(impl->file, impl->mdat_size_position) &&
				  std::fwrite(mdat_size_bytes, 1, 4, impl->file) == 4;

		MoovInfo info;
		info.sample_sizes = &impl->sample_sizes;
		info.width = impl->width;
		info.height = impl->height;
		info.frame_rate = impl->frame_rate;
		info.chunk_offset = impl->mdat_payload_position;

		const std::vector<uint8_t> moov = BuildMoov(info);

		// The media data is written sequentially, so the end of it is the end of the file.
		const uint64_t end_of_media = impl->mdat_payload_position + impl->mdat_payload_size;
		ok = ok && FileSeekTo(impl->file, static_cast<int64_t>(end_of_media));
		ok = ok && std::fwrite(moov.data(), 1, moov.size(), impl->file) == moov.size();

		if (!ok)
			std::cerr << "Error: failed to finish writing the video file " << impl->file_name << "." << std::endl;

		std::fclose(impl->file);
		impl->file = nullptr;
	}

	bool VideoWriter::IsOpen() const
	{
		return impl != nullptr && impl->file != nullptr;
	}

	int VideoWriter::Width() const
	{
		return impl != nullptr ? impl->width : 0;
	}

	int VideoWriter::Height() const
	{
		return impl != nullptr ? impl->height : 0;
	}

	int VideoWriter::FrameRate() const
	{
		return impl != nullptr ? impl->frame_rate : 0;
	}

	int VideoWriter::Quality() const
	{
		return impl != nullptr ? impl->quality : 0;
	}

	int VideoWriter::FrameCount() const
	{
		return impl != nullptr ? impl->frames_written : 0;
	}

	bool VideoWriter::WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (!IsOpen())
		{
			std::cerr << "Error: no video file is open." << std::endl;
			return false;
		}

		if (frame.width != impl->width || frame.height != impl->height)
		{
			std::cerr << "Error: frame size " << frame.width << "x" << frame.height
					  << " does not match the video size " << impl->width << "x" << impl->height << "." << std::endl;
			return false;
		}

		// stb_image_write expects tightly packed pixels, so a padded stride is not passed on.
		impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height * 3);

		for (int y = 0; y < impl->height; y++)
		{
			for (int x = 0; x < impl->width; x++)
			{
				const Pixel<ImageFormat::RGB, uint8_t> pix = frame.GetPixel(x, y);
				const size_t idx = (static_cast<size_t>(y) * impl->width + x) * 3;

				impl->packed_pixels[idx] = pix.r;
				impl->packed_pixels[idx + 1] = pix.g;
				impl->packed_pixels[idx + 2] = pix.b;
			}
		}

		return EncodeFrame(impl->packed_pixels.data(), 3);
	}

	bool VideoWriter::WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (!IsOpen())
		{
			std::cerr << "Error: no video file is open." << std::endl;
			return false;
		}

		if (frame.width != impl->width || frame.height != impl->height)
		{
			std::cerr << "Error: frame size " << frame.width << "x" << frame.height
					  << " does not match the video size " << impl->width << "x" << impl->height << "." << std::endl;
			return false;
		}

		impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height);

		for (int y = 0; y < impl->height; y++)
		{
			for (int x = 0; x < impl->width; x++)
			{
				const Pixel<ImageFormat::GRAY, uint8_t> pix = frame.GetPixel(x, y);
				const size_t idx = static_cast<size_t>(y) * impl->width + x;

				impl->packed_pixels[idx] = pix.v;
			}
		}

		return EncodeFrame(impl->packed_pixels.data(), 1);
	}

	bool VideoWriter::EncodeFrame(const void* pixels, int components)
	{
		if (!IsOpen())
			return false;

		if (impl->format == VideoFormat::MP4_H264)
			return EncodeH264(pixels, components);

		impl->jpeg.clear();

		if (stbi_write_jpg_to_func(AppendJpegData, &impl->jpeg, impl->width, impl->height,
								   components, pixels, impl->quality) == 0 || impl->jpeg.empty())
		{
			std::cerr << "Error: failed to encode a JPEG frame." << std::endl;
			return false;
		}

		// The size field of the mdat box is 32 bits wide, so this container cannot hold
		// more than 4 GB of media data. Refusing the frame here keeps the file valid
		// instead of letting the size field wrap around.
		const uint64_t max_mdat_payload = 0xFFFFFFFFull - 8ull;
		if (impl->mdat_payload_size + impl->jpeg.size() > max_mdat_payload)
		{
			std::cerr << "Error: the video file " << impl->file_name
					  << " would exceed the 4 GB limit of this container." << std::endl;
			return false;
		}

		if (std::fwrite(impl->jpeg.data(), 1, impl->jpeg.size(), impl->file) != impl->jpeg.size())
		{
			std::cerr << "Error: failed to write a frame to the video file " << impl->file_name << "." << std::endl;
			return false;
		}

		impl->sample_sizes.push_back(static_cast<uint32_t>(impl->jpeg.size()));
		impl->mdat_payload_size += impl->jpeg.size();
		impl->frames_written++;

		return true;
	}

	// Converts packed RGB (components = 3) or grayscale (components = 1) pixels into the
	// planar 4:2:0 frame the H.264 encoder expects: a full size luma plane followed by the
	// two half size chroma planes. Chroma is averaged over every 2 x 2 block; because the
	// frame size is a multiple of 16, the blocks always cover the frame completely.
	void VideoWriter::ToI420(const void* pixels, int components)
	{
		const uint8_t* source = static_cast<const uint8_t*>(pixels);
		const int width = impl->width;
		const int height = impl->height;
		const int chroma_width = width / 2;

		uint8_t* y_plane = impl->h264.i420.data();
		uint8_t* u_plane = y_plane + static_cast<size_t>(width) * height;
		uint8_t* v_plane = u_plane + static_cast<size_t>(chroma_width) * (height / 2);

		for (int y = 0; y < height; y++)
		{
			uint8_t* y_row = y_plane + static_cast<size_t>(y) * width;

			for (int x = 0; x < width; x++)
			{
				if (components >= 3)
				{
					const size_t index = (static_cast<size_t>(y) * width + x) * 3;
					y_row[x] = Luma(source[index], source[index + 1], source[index + 2]);
				}
				else
					y_row[x] = Luma(source[static_cast<size_t>(y) * width + x],
									source[static_cast<size_t>(y) * width + x],
									source[static_cast<size_t>(y) * width + x]);
			}
		}

		for (int y = 0; y < height; y += 2)
		{
			uint8_t* u_row = u_plane + static_cast<size_t>(y / 2) * chroma_width;
			uint8_t* v_row = v_plane + static_cast<size_t>(y / 2) * chroma_width;

			for (int x = 0; x < width; x += 2)
			{
				int u_sum = 0;
				int v_sum = 0;

				for (int dy = 0; dy < 2; dy++)
				{
					for (int dx = 0; dx < 2; dx++)
					{
						const size_t index = (static_cast<size_t>(y + dy) * width + x + dx) * components;
						const uint8_t r = source[index];
						const uint8_t g = components >= 3 ? source[index + 1] : source[index];
						const uint8_t b = components >= 3 ? source[index + 2] : source[index];

						u_sum += ChromaU(r, g, b);
						v_sum += ChromaV(r, g, b);
					}
				}

				u_row[x / 2] = static_cast<uint8_t>((u_sum + 2) / 4);
				v_row[x / 2] = static_cast<uint8_t>((v_sum + 2) / 4);
			}
		}
	}

	bool VideoWriter::EncodeH264(const void* pixels, int components)
	{
		if (impl->h264.encoder == nullptr || impl->h264.scratch == nullptr)
			return false;

		ToI420(pixels, components);

		const int width = impl->width;
		const int height = impl->height;
		uint8_t* y_plane = impl->h264.i420.data();

		H264E_io_yuv_t frame;
		std::memset(&frame, 0, sizeof(frame));
		frame.yuv[0] = y_plane;
		frame.stride[0] = width;
		frame.yuv[1] = y_plane + static_cast<size_t>(width) * height;
		frame.stride[1] = width / 2;
		frame.yuv[2] = y_plane + static_cast<size_t>(width) * height * 5 / 4;
		frame.stride[2] = width / 2;

		H264E_run_param_t run;
		std::memset(&run, 0, sizeof(run));
		run.encode_speed = H264E_SPEED_BALANCED;
		run.frame_type = H264E_FRAME_TYPE_DEFAULT; // the GOP size of the encoder decides
		run.qp_min = impl->h264_quantizer;         // both bounds equal: constant quality
		run.qp_max = impl->h264_quantizer;
		run.nalu_callback = &AppendNalu;
		run.nalu_callback_token = &impl->h264;

		// The encoder reports the NAL units through the callback, which forwards them to
		// the multiplexer, so the buffer it points to here is not used.
		uint8_t* coded_data = nullptr;
		int sizeof_coded_data = 0;

		const int error = H264E_encode(impl->h264.encoder, impl->h264.scratch, &run, &frame,
									   &coded_data, &sizeof_coded_data);

		if (error != H264E_STATUS_SUCCESS)
		{
			std::cerr << "Error: failed to encode an H.264 frame (encoder status " << error << ")." << std::endl;
			return false;
		}

		if (impl->h264.failed)
		{
			std::cerr << "Error: failed to store an H.264 frame in the video file " << impl->file_name << "." << std::endl;
			impl->h264.failed = false;
			return false;
		}

		impl->frames_written++;

		return true;
	}
}
