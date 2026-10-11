#include "UI/Panel/MediaPlayerPanel.h"

#include "UI/Component/Button.h"
#include "UI/Component/Label.h"
#include "UI/Component/VideoView.h"
#include "XCore/Timer/Timer.h"

#include <string>
#include <utility>

namespace X_Y
{
	MediaPlayerPanel::MediaPlayerPanel()
		: m_VideoView(new VideoView()),
		  m_PlayPauseButton(new Button()),
		  m_StopButton(new Button()),
		  m_Status(new Label())
	{
		m_PlayPauseButton->SetText("Play");
		m_PlayPauseButton->SetTextColor(0xFFFFFFFF);
		m_PlayPauseButton->SetBgColor(0xFF42464D);
		m_PlayPauseButton->SetHoverColor(0xFF565C64);
		m_PlayPauseButton->SetOnClick([this]
		{
			if (m_Player.IsPlaying())
			{
				m_Player.Pause();
				m_PlayPauseButton->SetText("Play");
			}
			else if (m_Player.Play())
			{
				m_PlayPauseButton->SetText("Pause");
			}
			else
			{
				m_Status->SetText(m_Player.GetErrorMessage());
			}
			RequestRepaint();
		});
		m_StopButton->SetText("Stop");
		m_StopButton->SetTextColor(0xFFFFFFFF);
		m_StopButton->SetBgColor(0xFF42464D);
		m_StopButton->SetHoverColor(0xFF565C64);
		m_StopButton->SetOnClick([this]
		{
			if (m_Player.IsOpen() && m_Player.Stop())
			{
				m_VideoView->ClearFrame();
				m_PlayPauseButton->SetText("Play");
			}
			else if (!m_Player.GetErrorMessage().empty())
			{
				m_Status->SetText(m_Player.GetErrorMessage());
			}
			RequestRepaint();
		});

		m_Status->SetColor(0xFFFFFFFF);
		m_Status->SetBgColor(0xFF30343B);
		m_Status->SetText("Drop a video file here or open one from the test app.");

		AddComponent(m_VideoView);
		AddComponent(m_PlayPauseButton);
		AddComponent(m_StopButton);
		AddComponent(m_Status);

		m_FrameTicker = std::make_unique<Ticker>();
		m_FrameTicker->Start(16, [this]
		{
			UpdateVideoFrame();
		});
	}

	MediaPlayerPanel::~MediaPlayerPanel()
	{
		if (m_FrameTicker)
		{
			m_FrameTicker->Stop();
			m_FrameTicker->Join();
		}
		m_Player.Close();
	}

	bool MediaPlayerPanel::OpenFile(const XPath &path)
	{
		return FileDropPanel::OpenFile(path);
	}

	void MediaPlayerPanel::OnLayout()
	{
		constexpr int controlsHeight = 34;
		constexpr int statusHeight = 24;
		const int videoHeight = m_H > controlsHeight + statusHeight
									? m_H - controlsHeight - statusHeight
									: 0;
		m_VideoView->SetRect(0, 0, m_W, videoHeight);
		m_PlayPauseButton->SetRect(0, videoHeight, 80, controlsHeight);
		m_StopButton->SetRect(80, videoHeight, 80, controlsHeight);
		m_Status->SetRect(
			0,
			videoHeight + controlsHeight,
			m_W,
			m_H - videoHeight - controlsHeight);
	}

	void MediaPlayerPanel::OnPaint(Canvas &canvas)
	{
		canvas.FillRect(0, 0, m_W, m_H, 0xFF151515);
		FileDropPanel::OnPaint(canvas);
	}

	bool MediaPlayerPanel::OnOpenFiles(const std::vector<XPath> &files)
	{
		m_VideoView->ClearFrame();
		m_PlayPauseButton->SetText("Play");
		for (const XPath &file : files)
		{
			if (!m_Player.Open(file.Path()))
				continue;

			const Decode::MediaInfo info = m_Player.GetMediaInfo();
			if (!info.HasVideo)
			{
				m_Status->SetText("This media file has no video stream.");
			}
			else
			{
				m_Status->SetText(
					file.getName() + " | " +
					std::to_string(info.Video.Width) + " x " +
					std::to_string(info.Video.Height));
			}
			m_PlayPauseButton->SetText("Pause");
			if (!m_Player.Play())
			{
				m_PlayPauseButton->SetText("Play");
				m_Status->SetText(m_Player.GetErrorMessage());
				return false;
			}
			RequestRepaint();
			return true;
		}

		m_PlayPauseButton->SetText("Play");
		m_Status->SetText(
			m_Player.GetErrorMessage().empty()
				? "Could not open a playable media file."
				: m_Player.GetErrorMessage());
		return false;
	}

	void MediaPlayerPanel::UpdateVideoFrame()
	{
		Decode::MediaFrame frame;
		if (m_Player.TakeVideoFrame(frame))
			m_VideoView->SetFrame(std::move(frame));
	}
}
