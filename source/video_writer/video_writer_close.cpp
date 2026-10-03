#include "video_writer_imp.hpp"

namespace qlm
{
	namespace
	{
		// Everything the index (moov) needs to know about the media data.
		struct MoovInfo
		{
			const std::vector<uint32_t>* sample_sizes = nullptr;
			int width = 0;
			int height = 0;
			int frame_rate = 0;
			uint64_t chunk_offset = 0; // file position of the first sample byte
		};

		// The unity matrix, stored in 16.16 / 2.30 fixed point.
		void AppendUnityMatrix(std::vector<uint8_t>& out)
		{
			AppendU32(out, 0x00010000); AppendU32(out, 0x00000000); AppendU32(out, 0x00000000);
			AppendU32(out, 0x00000000); AppendU32(out, 0x00010000); AppendU32(out, 0x00000000);
			AppendU32(out, 0x00000000); AppendU32(out, 0x00000000); AppendU32(out, 0x40000000);
		}

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
			if (impl->h264.encoder != nullptr)
			{
				impl->h264.encoder->Uninitialize();
				WelsDestroySVCEncoder(impl->h264.encoder);
				impl->h264.encoder = nullptr;
			}

			impl->h264.picture = SSourcePicture{};
			impl->h264.i420.clear();
			impl->h264.frame_index = 0;

			std::fclose(impl->file);
			impl->file = nullptr;
		}
		else // Motion-JPEG
		{
			// Patch the media data size, then append the index.
			const uint64_t mdat_size = impl->mdat_payload_size + 8;
			const uint8_t mdat_size_bytes[4] =
			{
				static_cast<uint8_t>((mdat_size >> 24) & 0xFF),
				static_cast<uint8_t>((mdat_size >> 16) & 0xFF),
				static_cast<uint8_t>((mdat_size >> 8) & 0xFF),
				static_cast<uint8_t>(mdat_size & 0xFF)
			};

			bool ok = FileSeekTo(impl->file, impl->mdat_size_position) && std::fwrite(mdat_size_bytes, 1, 4, impl->file) == 4;

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
	}
}
