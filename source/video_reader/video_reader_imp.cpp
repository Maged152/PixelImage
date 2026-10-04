#include "video_reader_imp.hpp"


namespace qlm
{
	// Creates the decoder and puts it in the state a stream is started from. A restart needs a
	// decoder that has not been told the end of a stream, so the same parameters are what the first
	// start and every restart use. Decoding runs on the calling thread, and the decoder is asked to
	// keep quiet about what it conceals: the reader does not give it a callback to log to, and a
	// damaged sample is not something it reports to whoever reads the frames.
	bool VideoReader::Impl::StartH264Decoder()
	{
		if (h264_decoder == nullptr && WelsCreateDecoder(&h264_decoder) != 0)
			return false;

		if (h264_decoder == nullptr)
			return false;

		SDecodingParam param{};
		param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
		param.eEcActiveIdc = ERROR_CON_SLICE_COPY;   // conceal a damaged slice instead of failing on it
		param.uiTargetDqLayer = UCHAR_MAX;           // decode the highest temporal layer there is
		param.bParseOnly = false;

		if (h264_decoder->Initialize(&param) != 0)
		{
			WelsDestroyDecoder(h264_decoder);
			h264_decoder = nullptr;
			return false;
		}

		int trace_level = WELS_LOG_QUIET;
		h264_decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &trace_level);

		return true;
	}

	// Starts the decoder of an H.264 track and checks that it can produce the frame the first sample
	// holds. The parameter sets the container carries are the only headers the decoder ever sees,
	// because an MP4 track keeps them in its sample entry instead of in front of the samples.
	bool VideoReader::Impl::OpenH264(const size_t sample_bytes, const std::string& file_name)
	{
		if (!StartH264Decoder())
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the H.264 decoder cannot be started." << std::endl;
			return false;
		}

		h264 = true;

		if (!DecodeH264Headers())
		{
			std::cerr << "Error loading video file " << file_name << ": cannot decode the H.264 parameter sets of the track: "
					  << H264StatusText(h264_status) << "." << std::endl;
			return false;
		}

		if (sample_bytes == 0)
		{
			std::cerr << "Error loading video file " << file_name << ": the first video sample is empty." << std::endl;
			return false;
		}

		if (!DecodeH264Sample(0))
		{
			if (h264_status != 0)
				std::cerr << "Error loading video file " << file_name << ": cannot decode the first H.264 sample: "
						  << H264StatusText(h264_status) << "." << std::endl;
			return false;
		}

		if (!h264_convertible)
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the H.264 track is not an 8-bit 4:2:0 stream, the only format VideoReader converts." << std::endl;
			return false;
		}

		h264_next_sample = 1;

		if (h264_decoded.empty())
		{
			// The H.264 DPB may hold the first frame until more samples arrive (reordering).
			// Decode more samples until a frame appears, or flush the decoder.
			while (h264_decoded.empty() && h264_next_sample < static_cast<int>(demux.track[track].sample_count))
			{
				if (!DecodeH264Sample(h264_next_sample))
				{
					if (h264_status != 0)
						std::cerr << "Error loading video file " << file_name << ": cannot decode H.264 sample "
								  << h264_next_sample << ": " << H264StatusText(h264_status) << "." << std::endl;
					return false;
				}

				h264_next_sample++;
			}

			// If still no frame, flush the decoder to force out any remaining frames
			if (h264_decoded.empty())
			{
				if (!FlushH264())
				{
					std::cerr << "Error loading video file " << file_name
							  << ": flushing the H.264 decoder failed." << std::endl;
					return false;
				}

				h264_flushed = true;   // the end of the stream is behind the decoder now
			}
		}

		if (h264_decoded.empty())
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the H.264 decoder produced no frame for the first sample." << std::endl;
			return false;
		}

		width = h264_decoded.front().width;
		height = h264_decoded.front().height;

		return true;
	}

	// Releases the decoder and the frames it produced.
	void VideoReader::Impl::CloseH264()
	{
		if (h264_decoder != nullptr)
		{
			h264_decoder->Uninitialize();
			WelsDestroyDecoder(h264_decoder);
			h264_decoder = nullptr;
		}

		h264_annex_b.clear();
		h264_decoded.clear();
		h264 = false;
		h264_next_sample = 0;
		h264_frames_stored = 0;
		h264_flushed = false;
		h264_convertible = true;
		h264_status = 0;
	}

	// Hands the decoder the SPS and the PPS of the track, which the demuxer hands out one at a time
	// until the list ends. Each one comes with the NAL header that introduces it, so it only needs
	// the start code in front of it, and the two go out together with the sample that follows: a
	// track that carries its parameter sets keeps them in front of the slices that use them.
	bool VideoReader::Impl::DecodeH264Headers()
	{
		for (int index = 0;; index++)
		{
			int sps_bytes = 0;
			const uint8_t* sps = static_cast<const uint8_t*>(MP4D_read_sps(&demux, static_cast<unsigned>(track), index, &sps_bytes));

			if (sps == nullptr)
				break;

			AppendH264Nalu(sps, sps + sps_bytes);
		}

		for (int index = 0;; index++)
		{
			int pps_bytes = 0;
			const uint8_t* pps = static_cast<const uint8_t*>(MP4D_read_pps(&demux, static_cast<unsigned>(track), index, &pps_bytes));

			if (pps == nullptr)
				break;

			AppendH264Nalu(pps, pps + pps_bytes);
		}

		return true;
	}

	// Puts the decoder back to the state StartH264Decoder() leaves it in, which is the only way to
	// reach a frame that lies before the frames it produced already. A decoder that has been told
	// the stream is over cannot be fed more of it, so it is put away and replaced.
	bool VideoReader::Impl::RestartH264()
	{
		if (h264_decoder != nullptr)
		{
			h264_decoder->Uninitialize();
			WelsDestroyDecoder(h264_decoder);
			h264_decoder = nullptr;
		}

		h264_annex_b.clear();
		h264_decoded.clear();
		h264_next_sample = 0;
		h264_frames_stored = 0;
		h264_flushed = false;
		h264_convertible = true;
		h264_status = 0;

		// The parameter sets went with the decoder that was dropped
		return StartH264Decoder() && DecodeH264Headers();
	}

	// ------------------------------------------------------------------------------------------
	// Builds the access unit the decoder is handed next. A NAL unit an MP4 track carries has no
	// start code, and the decoder reads nothing else, so each one is put behind the four bytes that
	// introduce it. The unit is handed over as one piece when the sample that holds it is complete,
	// because the pictures of an access unit belong together.
	// ------------------------------------------------------------------------------------------
	void VideoReader::Impl::AppendH264Nalu(const uint8_t* begin, const uint8_t* end)
	{
		if (begin == nullptr || end <= begin)
			return;   // a NAL unit of no bytes carries nothing

		static constexpr uint8_t start_code[] = { 0x00, 0x00, 0x00, 0x01 };

		h264_annex_b.insert(h264_annex_b.end(), start_code, start_code + sizeof(start_code));
		h264_annex_b.insert(h264_annex_b.end(), begin, end);
	}

	// Tells the decoder that the stream is over and takes the frames it still holds, which a stream
	// that reorders its pictures keeps back until the end of it. Being told is what makes them come
	// out, so the end of the stream is stated as an option before the frames are asked for. The
	// track holds no more frames than it states, which is the bound the loop cannot pass: the
	// decoder hands out one picture per call and then holds none, but no call here is repeated on
	// the word of the decoder alone.
	bool VideoReader::Impl::FlushH264()
	{
		int end_of_stream = 1;
		h264_decoder->SetOption(DECODER_OPTION_END_OF_STREAM, &end_of_stream);

		for (int taken = 0; taken <= frame_count; taken++)
		{
			unsigned char* planes[3] = { nullptr, nullptr, nullptr };
			SBufferInfo info{};
			const int status = static_cast<int>(h264_decoder->FlushFrame(planes, &info));

			// A picture the decoder produced is taken from it before the status of the call is
			// read, because a call that fails may still have produced one
			if (info.iBufferStatus == 1 && !StoreH264Picture(planes, info))
			{
				h264_status = 0;   // the picture could not be converted, which is not a status
				return false;
			}

			if (H264StatusIsError(status))
			{
				h264_status = status;
				return false;
			}

			// The decoder hands out one picture per call and then holds no more
			if (info.iBufferStatus != 1)
				return true;
		}

		return true;   // the bound was reached, which no track the reader reads does
	}

	// Copies a picture the decoder produced into the queue. It hands its pictures out in display
	// order, so the queue holds the pictures of the last samples in display order as well, and the
	// number of pictures that went into it is the index of the picture at its front.
	bool VideoReader::Impl::StoreH264Picture(unsigned char* const planes[3], const SBufferInfo& info)
	{
		const SSysMEMBuffer& buffer = info.UsrData.sSystemBuffer;

		if (buffer.iFormat != videoFormatI420)
		{
			h264_convertible = false;   // the picture is not an 8-bit 4:2:0 one, the only format
			return false;               // the reader converts
		}

		const int strides[2] = { buffer.iStride[0], buffer.iStride[1] };
		PlanarFrame frame;

		if (!CopyDecoderFrame(planes, strides, buffer.iWidth, buffer.iHeight, frame))
		{
			h264_convertible = false;
			return false;
		}

		h264_decoded.push_back(std::move(frame));
		h264_frames_stored++;

		return true;
	}

	// Hands the access unit that was built to the decoder and takes the picture it produced for it,
	// if it produced one: a stream that reorders its frames holds its pictures back for a while, and
	// one that does not hands out a picture for every sample that is fed to it.
	bool VideoReader::Impl::FeedH264()
	{
		unsigned char* planes[3] = { nullptr, nullptr, nullptr };
		SBufferInfo info{};
		const int status = static_cast<int>(h264_decoder->DecodeFrameNoDelay(
			h264_annex_b.data(), static_cast<int>(h264_annex_b.size()), planes, &info));

		h264_annex_b.clear();

		// The picture comes out of the decoder before the status of the call is read, because a call
		// that reports a damaged sample may still have produced one
		if (info.iBufferStatus == 1 && !StoreH264Picture(planes, info))
		{
			h264_status = 0;   // the picture could not be converted, which is not a status
			return false;
		}

		if (H264StatusIsError(status))
		{
			h264_status = status;
			return false;
		}

		return true;
	}

	// Sends one sample to the decoder, which is one access unit of the track. A sample of an MP4
	// H.264 track holds one or more NAL units, each prefixed with its length; the size of that field
	// is stated in the sample entry, which the demuxer does not hand out, so it is read from the
	// sample itself.
	bool VideoReader::Impl::DecodeH264Sample(const int sample_index)
	{
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		const MP4D_file_offset_t offset = MP4D_frame_offset(&demux, static_cast<unsigned>(track),
															static_cast<unsigned>(sample_index),
															&sample_bytes, &timestamp, &sample_duration);

		if (sample_bytes == 0 || offset + sample_bytes > file_data.size())
		{
			h264_status = 0;
			std::cerr << "Error: video sample " << sample_index << " is out of range." << std::endl;
			return false;
		}

		const uint8_t* sample = file_data.data() + offset;
		const size_t length_size = NaluLengthSize(sample, sample_bytes);

		if (length_size == 0)
		{
			h264_status = 0;
			std::cerr << "Error: video sample " << sample_index << " does not hold length prefixed H.264 NAL units." << std::endl;
			return false;
		}

		size_t position = 0;
		while (position < sample_bytes)
		{
			size_t nalu_bytes = 0;

			for (size_t i = 0; i < length_size; i++)
				nalu_bytes = (nalu_bytes << 8) | sample[position + i];

			AppendH264Nalu(sample + position + length_size, sample + position + length_size + nalu_bytes);
			position += length_size + nalu_bytes;
		}

		// The sample holds a whole access unit, which the decoder takes as one piece
		return FeedH264();
	}

	// Prepares the frame at frame_index of an H.264 track. The decoder decodes in order, so every
	// sample up to the one that holds the frame is fed to it; the frames of the samples that come
	// after that one stay in the queue, for the reads that follow.
	const PlanarFrame* VideoReader::Impl::PrepareH264Frame(const int frame_index)
	{
		// A frame that lies before the oldest frame the queue holds can only be
		// reached by decoding the track from its first sample again
		if (!h264_decoded.empty() && frame_index < DecodedFrameBase())
		{
			if (!RestartH264())
			{
				std::cerr << "Error: cannot decode the H.264 track again: " << H264StatusText(h264_status) << "." << std::endl;
				return nullptr;
			}
		}

		while (frame_index >= DecodedFrameBase() + static_cast<int>(h264_decoded.size()))
		{
			if (h264_next_sample < frame_count)
			{
				if (!DecodeH264Sample(h264_next_sample))
				{
					if (h264_status != 0)
						std::cerr << "Error: cannot decode video sample " << h264_next_sample << ": "
						          << H264StatusText(h264_status) << "." << std::endl;
					return nullptr;
				}

				h264_next_sample++;
				continue;   // the decoder holds frames back, so the wanted frame may still be in it
			}
			else if (!h264_flushed)
			{
				// Reading past the last sample: the flush hands out the frames the decoder still
				// holds, and the last frames of the track come from there
				h264_flushed = true;

				if (!FlushH264())
				{
					std::cerr << "Error: cannot flush the H.264 decoder: " << H264StatusText(h264_status) << "." << std::endl;
					return nullptr;
				}
			}
			else
			{
				std::cerr << "Error: the H.264 track ends before frame " << frame_index << "." << std::endl;
				return nullptr;
			}

			if (!h264_convertible)
			{
				std::cerr << "Error: the H.264 track is not an 8-bit 4:2:0 stream, the only format VideoReader converts."
				          << std::endl;
				return nullptr;
			}
		}

		// Position of the frame inside the queue: the queue holds the frames taken from the decoder
		// last, starting with DecodedFrameBase(), so the wanted frame sits this far into it.
		const size_t position = static_cast<size_t>(frame_index - DecodedFrameBase());

		if (position >= h264_decoded.size())
		{
			std::cerr << "Error: cannot decode video frame " << frame_index << "." << std::endl;
			return nullptr;
		}

		// The frame the caller wants, held by the queue that keeps it for the reads that follow
		const PlanarFrame& frame = h264_decoded[position];
		width = frame.width;
		height = frame.height;

		// The presentation time is that of the sample the frame belongs to, which is the sample of
		// the same index. minimp4 ignores the composition offsets, so a stream whose decoding order
		// differs from its presentation order, which needs B-frames, is only approximated.
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		MP4D_frame_offset(&demux, static_cast<unsigned>(track), static_cast<unsigned>(frame_index),
											&sample_bytes, &timestamp, &sample_duration);
		time = timescale > 0 ? static_cast<double>(timestamp) / timescale : 0.0;

		// Drop the frames up to the one that was just read: they are not needed
		// any more, and keeping them would gather the track in memory. What
		// stays is the frame that was read last and the frames after it.
		while (h264_decoded.size() > 1 && DecodedFrameBase() < frame_index)
			h264_decoded.pop_front();

		return &frame;
	}

	// Index of the oldest frame the queue holds. The decoder hands its frames out in display order
	// and starts with frame 0, and every frame it produced went into the queue, so the frames the
	// queue still holds are the last ones of them.
	int VideoReader::Impl::DecodedFrameBase() const
	{
		return h264_frames_stored - static_cast<int>(h264_decoded.size());
	}
}
