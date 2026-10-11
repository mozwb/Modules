#include "Component/VideoView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace X_Y
{
	void VideoView::SetFrame(Decode::MediaFrame &&frame)
	{
		{
			std::lock_guard lock(m_FrameMutex);
			m_Frame = std::move(frame);
		}
		RequestRepaint();
	}

	void VideoView::ClearFrame()
	{
		{
			std::lock_guard lock(m_FrameMutex);
			m_Frame = {};
		}
		RequestRepaint();
	}

	void VideoView::OnPaint(Canvas &canvas)
	{
		std::lock_guard lock(m_FrameMutex);
		if (m_Frame.Type != Decode::MediaStreamType::Video ||
			!m_Frame.Data.Data ||
			m_Frame.Width == 0 ||
			m_Frame.Height == 0 ||
			GetWidth() <= 0 ||
			GetHeight() <= 0)
			return;

		const double scale = std::min(
			static_cast<double>(GetWidth()) / m_Frame.Width,
			static_cast<double>(GetHeight()) / m_Frame.Height);
		const int drawWidth =
			std::max(1, static_cast<int>(std::round(m_Frame.Width * scale)));
		const int drawHeight =
			std::max(1, static_cast<int>(std::round(m_Frame.Height * scale)));
		const int drawX = (GetWidth() - drawWidth) / 2;
		const int drawY = (GetHeight() - drawHeight) / 2;
		canvas.DrawImage(
			m_Frame.Data.Data,
			m_Frame.Data.Size,
			m_Frame.Width,
			m_Frame.Height,
			4,
			drawX,
			drawY,
			drawWidth,
			drawHeight);
	}
}
