#pragma once

#include "Component.h"
#include "Decoder/Media.h"

#include <mutex>

namespace X_Y
{
	class VideoView final : public Component
	{
	public:
		void SetFrame(Decode::MediaFrame &&frame);
		void ClearFrame();
		void OnPaint(Canvas &canvas) override;

	private:
		std::mutex m_FrameMutex;
		Decode::MediaFrame m_Frame;
	};
}
