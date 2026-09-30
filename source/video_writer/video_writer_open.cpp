#include "video_writer_imp.hpp"

namespace qlm
{
	namespace
	{
		bool ValidateInputs(const int frame_width, const int frame_height, const int frame_rate, const int quality, const VideoFormat format)
		{
			if (format != VideoFormat::MP4_MJPEG && format != VideoFormat::MP4_H264)
			{
				std::cerr << "Error: unsupported video format." << std::endl;
				return false;
			}

			if (frame_width <= 0 || frame_height <= 0 || frame_width > 65535 || frame_height > 65535)
			{
				std::cerr << "Error: invalid video dimensions " << frame_width << "x" << frame_height
						  << ". They must be positive and at most 65535." << std::endl;
				return false;
			}

			if (frame_rate <= 0 || frame_rate > 90000)
			{
				std::cerr << "Error: invalid frame rate " << frame_rate << "." << std::endl;
				return false;
			}

			if (quality < 1 || quality > 100)
			{
				std::cerr << "Error: invalid quality " << quality
						  << ". It must be in the range [1, 100]." << std::endl;
				return false;
			}

			if (format == VideoFormat::MP4_H264 && (frame_width % 16 != 0 || frame_height % 16 != 0))
			{
				std::cerr << "Error: for H.264 encoding, video dimensions must be multiples of 16."
						  << std::endl;
				return false;
			}

			return true;
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

		/*************************** Motion-JPEG  ***************************/
		

		int64_t FilePosition(std::FILE* file)
		{
		#if defined(_MSC_VER) || defined(__MINGW32__)
			return static_cast<int64_t>(_ftelli64(file));
		#else
			return static_cast<int64_t>(std::ftell(file));
		#endif
		}
	}

	bool VideoWriter::Open(const std::string& file_name, int frame_width, int frame_height, int frame_rate,
						int quality, VideoFormat format)
	{
		Close();

		if (impl == nullptr) // this object was moved from, so it owns nothing yet
			impl = std::make_unique<Impl>();

		if (!ValidateInputs(frame_width, frame_height, frame_rate, quality, format))
		{
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
		impl->quality = quality;
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
		}
		else
		{
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
		}

		return true;
	}
}
