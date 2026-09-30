#include "video_writer_imp.hpp"

namespace qlm
{
	namespace
	{
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

		void AppendNalu(const unsigned char* nalu_data, int sizeof_nalu_data, void* token)
		{
			constexpr int start_code_bytes = 4;
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
	
		// stb_image_write callback that appends the encoded JPEG to a vector.
		void AppendJpegData(void* context, void* data, int size)
		{
			std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			out->insert(out->end(), bytes, bytes + size);
		}
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

	bool VideoWriter::EncodeMJPEG(const void *pixels, int components)
	{
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
}
