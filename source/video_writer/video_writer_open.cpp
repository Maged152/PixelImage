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

			if (format == VideoFormat::MP4_H264)
			{
				// The 4:2:0 sampling of the encoder needs a chroma sample per 2 x 2 luma block.
				if (frame_width % 2 != 0 || frame_height % 2 != 0)
				{
					std::cerr << "Error: for H.264 encoding, video dimensions must be even." << std::endl;
					return false;
				}

				// The smallest picture the encoder accepts is one macroblock.
				if (frame_width < 16 || frame_height < 16)
				{
					std::cerr << "Error: for H.264 encoding, video dimensions must be at least 16x16."
							  << std::endl;
					return false;
				}

				// The encoder limits the number of macroblocks per frame (MAX_MBS_PER_FRAME << 8).
				constexpr int64_t max_h264_pixels = 9437184;
				if (static_cast<int64_t>(frame_width) * frame_height > max_h264_pixels)
				{
					std::cerr << "Error: for H.264 encoding, " << frame_width << "x" << frame_height
							  << " is more than the " << max_h264_pixels
							  << " pixels the encoder supports in one picture." << std::endl;
					return false;
				}
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

			// OpenH264 pads the size it is given up to whole macroblocks and records the size
			// it was given as frame cropping in the SPS, so the buffer that is filled here
			// holds the coded size while the frames are reported at the size they were
			// written as.
			impl->h264.coded_width = (frame_width + 15) & ~15;
			impl->h264.coded_height = (frame_height + 15) & ~15;
			impl->h264_quantizer = QualityToQuantizer(impl->quality);

			if (WelsCreateSVCEncoder(&impl->h264.encoder) != 0 || impl->h264.encoder == nullptr)
			{
				std::cerr << "Error: cannot create the H.264 encoder." << std::endl;
				Close();
				return false;
			}

			// The defaults come first: the encoder rejects a parameter set that leaves a
			// field it needs at zero.
			SEncParamExt params;
			impl->h264.encoder->GetDefaultParams(&params);

			params.iUsageType = CAMERA_VIDEO_REAL_TIME; // one frame in, one frame out, no lookahead
			params.iPicWidth = frame_width;             // the size the caller asked for
			params.iPicHeight = frame_height;
			params.iRCMode = RC_OFF_MODE;               // constant quantizer, no bit rate target
			params.fMaxFrameRate = static_cast<float>(frame_rate);
			params.iTemporalLayerNum = 1;               // no temporal scalability
			params.iSpatialLayerNum = 1;                // one spatial layer: plain AVC
			params.uiIntraPeriod = static_cast<unsigned int>(frame_rate); // key frame every second
			params.eSpsPpsIdStrategy = CONSTANT_ID;     // the parameter sets never change, so the
													   // multiplexer records them in avcC once
			params.bPrefixNalAddingCtrl = false;        // no prefix NAL units
			params.bEnableFrameCroppingFlag = true;     // record the padding as cropping
			params.bEnableFrameSkip = false;            // a skipped frame would desync frames_written
			params.iMultipleThreadIdc = 1;              // one thread, so the output is reproducible
			params.iEntropyCodingModeFlag = 1;          // CABAC, the smaller of the two encodings

			SSpatialLayerConfig& layer = params.sSpatialLayers[0];
			layer.iVideoWidth = frame_width;
			layer.iVideoHeight = frame_height;
			layer.fFrameRate = static_cast<float>(frame_rate);
			layer.iSpatialBitrate = 0;                  // unused while the rate control is off
			layer.iDLayerQp = impl->h264_quantizer;
			layer.uiProfileIdc = PRO_UNKNOWN;           // let the encoder pick the profile
			layer.uiLevelIdc = LEVEL_UNKNOWN;
			layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

			if (impl->h264.encoder->InitializeExt(&params) != 0)
			{
				std::cerr << "Error: cannot start the H.264 encoder for " << frame_width << "x"
						  << frame_height << "." << std::endl;
				Close();
				return false;
			}

			// The encoder announces itself and reports every frame on the standard streams
			int trace_level = WELS_LOG_QUIET;
			impl->h264.encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace_level);

			impl->h264.picture.iColorFormat = videoFormatI420;
			impl->h264.picture.iPicWidth = frame_width;
			impl->h264.picture.iPicHeight = frame_height;
			impl->h264.picture.iStride[0] = impl->h264.coded_width;
			impl->h264.picture.iStride[1] = impl->h264.coded_width / 2;
			impl->h264.picture.iStride[2] = impl->h264.coded_width / 2;

			impl->h264.i420.resize(static_cast<size_t>(impl->h264.coded_width)
								   * impl->h264.coded_height * 3 / 2);
			impl->h264.frame_ticks = 90000 / frame_rate;
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
