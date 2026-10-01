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

		void AppendJpegData(void* context, void* data, int size)
		{
			std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			out->insert(out->end(), bytes, bytes + size);
		}
	}

	// Converts packed RGB (components = 3) or grayscale (components = 1) pixels into the
	// planar 4:2:0 frame the H.264 encoder expects: a full size luma plane followed by the
	// two half size chroma planes. Chroma is averaged over every 2 x 2 block. The encoder
	// codes the size rounded up to whole macroblocks, so the last real column and row are
	// repeated into the margin; the encoder records that margin as frame cropping, so it
	// never reaches the decoded picture.
	void VideoWriter::ToI420(const void* pixels, int components)
	{
		const uint8_t* source = static_cast<const uint8_t*>(pixels);
		const int width = impl->width;
		const int height = impl->height;
		const int coded_width = impl->h264.coded_width;
		const int coded_height = impl->h264.coded_height;
		const int chroma_width = coded_width / 2;
		const int chroma_height = coded_height / 2;

		uint8_t* y_plane = impl->h264.i420.data();
		uint8_t* u_plane = y_plane + static_cast<size_t>(coded_width) * coded_height;
		uint8_t* v_plane = u_plane + static_cast<size_t>(chroma_width) * chroma_height;

		// Sampling the source with clamped coordinates fills the coded picture in one pass:
		// inside it the coordinate is the pixel itself, in the margin it is the edge pixel.
		for (int y = 0; y < coded_height; y++)
		{
			const int source_y = std::min(y, height - 1);
			uint8_t* y_row = y_plane + static_cast<size_t>(y) * coded_width;

			for (int x = 0; x < coded_width; x++)
			{
				const size_t index = (static_cast<size_t>(source_y) * width + std::min(x, width - 1))
									 * components;
				const uint8_t r = source[index];
				const uint8_t g = components >= 3 ? source[index + 1] : source[index];
				const uint8_t b = components >= 3 ? source[index + 2] : source[index];

				y_row[x] = Luma(r, g, b);
			}
		}

		for (int y = 0; y < chroma_height; y++)
		{
			uint8_t* u_row = u_plane + static_cast<size_t>(y) * chroma_width;
			uint8_t* v_row = v_plane + static_cast<size_t>(y) * chroma_width;

			for (int x = 0; x < chroma_width; x++)
			{
				int u_sum = 0;
				int v_sum = 0;

				for (int dy = 0; dy < 2; dy++)
				{
					for (int dx = 0; dx < 2; dx++)
					{
						const int source_y = std::min(y * 2 + dy, height - 1);
						const int source_x = std::min(x * 2 + dx, width - 1);
						const size_t index = (static_cast<size_t>(source_y) * width + source_x)
											 * components;
						const uint8_t r = source[index];
						const uint8_t g = components >= 3 ? source[index + 1] : source[index];
						const uint8_t b = components >= 3 ? source[index + 2] : source[index];

						u_sum += ChromaU(r, g, b);
						v_sum += ChromaV(r, g, b);
					}
				}

				u_row[x] = static_cast<uint8_t>((u_sum + 2) / 4);
				v_row[x] = static_cast<uint8_t>((v_sum + 2) / 4);
			}
		}
	}

	bool VideoWriter::EncodeH264(const void* pixels, int components)
	{
		if (impl->h264.encoder == nullptr)
			return false;

		ToI420(pixels, components);

		const size_t luma_size = static_cast<size_t>(impl->h264.coded_width) * impl->h264.coded_height;
		uint8_t* y_plane = impl->h264.i420.data();

		impl->h264.picture.pData[0] = y_plane;
		impl->h264.picture.pData[1] = y_plane + luma_size;
		impl->h264.picture.pData[2] = y_plane + luma_size + luma_size / 4;
		impl->h264.picture.uiTimeStamp = static_cast<long long>(impl->h264.frame_index) * 1000
										 / impl->frame_rate;

		SFrameBSInfo info;
		std::memset(&info, 0, sizeof(info));

		const int error = impl->h264.encoder->EncodeFrame(&impl->h264.picture, &info);

		if (error != cmResultSuccess)
		{
			std::cerr << "Error: failed to encode an H.264 frame (encoder status " << error << ")." << std::endl;
			return false;
		}

		// The access unit arrives as a list of layers, each holding a list of NAL units,
		// and every NAL unit carries the start code the multiplexer expects. The
		// multiplexer collects the parameter sets of the first frame into the avcC box,
		// marks the frames that the encoder made key frames as sync samples and builds the
		// sample table, so nothing of that has to be tracked here.
		for (int layer_index = 0; layer_index < info.iLayerNum; layer_index++)
		{
			const SLayerBSInfo& layer = info.sLayerInfo[layer_index];
			const unsigned char* nal = layer.pBsBuf;

			for (int nal_index = 0; nal_index < layer.iNalCount; nal_index++)
			{
				const int nal_size = layer.pNalLengthInByte[nal_index];

				if (mp4_h26x_write_nal(&impl->h264.writer, nal, nal_size,
									   impl->h264.frame_ticks) != MP4E_STATUS_OK)
				{
					std::cerr << "Error: failed to store an H.264 frame in the video file "
							  << impl->file_name << "." << std::endl;
					return false;
				}

				nal += nal_size;
			}
		}

		impl->h264.frame_index++;
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
