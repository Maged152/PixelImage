#pragma once

namespace qlm
{
	// Container and codec combination used by VideoWriter.
	//
	// MP4_MJPEG : ISO base media file format (ISO/IEC 14496-12, .mp4) holding a single
	//             Motion-JPEG video track, stored with the QuickTime 'jpeg' sample entry.
	//             Every frame is an independent JPEG image, so no video codec library
	//             is required and every frame is a random access point.
	//
	// MP4_H264  : the same container holding one H.264 (AVC) video track, written with
	//             the 'avc1' sample entry. Frames are encoded by the vendored minih264
	//             encoder and muxed by the vendored minimp4 multiplexer, so the files are
	//             far smaller than Motion-JPEG files of the same clip, at the price of a
	//             lossy inter-frame codec. See doc/video.md for the details, including
	//             the frame size rule and how quality maps onto the H.264 quantizer.
	enum class VideoFormat
	{
		MP4_MJPEG,
		MP4_H264,
	};
}