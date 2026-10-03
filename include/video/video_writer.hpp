#pragma once

#include "image.hpp"
#include "video_common.hpp"
#include <string>
#include <memory>

namespace qlm
{
	// Writes a video file one frame at a time.
	//
	// Frames are encoded with stb_image_write (JPEG) and muxed into an MP4 container,
	// which means the output is playable by FFmpeg based players, VLC, mpv and
	// QuickTime, but not by web browsers. There is no inter-frame compression and no
	// audio track; see doc/video.md for the details.
	//
	// A VideoWriter holds an open file between Open and Close, so it is movable but
	// not copyable.
	class VideoWriter
	{
	private:
		struct Impl;
		std::unique_ptr<Impl> impl;

		bool EncodeFrame(const void* pixels, int components);

		// Rejects (and reports) a frame whose size differs from the one Open was given.
		bool ValidateFrame(int frame_width, int frame_height) const;

		// Encodes a packed frame as H.264 and hands it to the multiplexer.
		bool EncodeH264(const void* pixels, int components);
		bool EncodeMJPEG(const void* pixels, int components);

		// Fills the planar 4:2:0 frame the encoder expects from packed RGB or grayscale
		// pixels, where components is 3 for RGB and 1 for GRAY.
		void ToI420(const void* pixels, int components);

	public:
		VideoWriter();
		~VideoWriter();

		VideoWriter(VideoWriter&& other) noexcept;
		VideoWriter& operator=(VideoWriter&& other) noexcept;

		VideoWriter(const VideoWriter&) = delete;
		VideoWriter& operator=(const VideoWriter&) = delete;

		// Opens file_name and prepares a video track of frame_width x frame_height.
		// frame_rate is the number of frames per second (an integer, e.g. 24, 25, 30, 60).
		// quality is 1 (worst) to 100 (best). It is the JPEG quality for MP4_MJPEG and is
		// mapped onto the H.264 quantizer for MP4_H264 (see doc/video.md for the table).
		//
		// MP4_H264 requires frame_width and frame_height to be even and at least 16, and at
		// most 9437184 pixels in the frame; the encoder codes whole macroblocks and records
		// the padding as cropping, so the frame reads back at the size it was written as.
		// MP4_MJPEG accepts any size up to 65535.
		bool Open(const std::string& file_name, int frame_width, int frame_height, int frame_rate,
				  int quality = 90, VideoFormat format = VideoFormat::MP4_H264);

		// Writes the index (moov) and closes the file. Called by the destructor too.
		void Close();

		bool IsOpen() const;

		// Frames must have the dimensions passed to Open.
		bool WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame);
		bool WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame);

		int Width() const;
		int Height() const;
		int FrameRate() const;
		int Quality() const;
		VideoFormat Format() const;

		// Number of frames written so far.
		int FrameCount() const;
	};
}
