#include <Render/EditorCamera.h>
#include "Input/Input.h"
#include <algorithm>
namespace X_Y
{
	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), Camera(RenderMath::Perspective(RenderMath::Radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = RenderMath::Perspective(RenderMath::Radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
	}

	void EditorCamera::UpdateView()
	{
		// m_Yaw = m_Pitch = 0.0f; // Lock the camera's rotation
		m_Position = CalculatePosition();

		RenderMath::Quat orientation = GetOrientation();
		m_ViewMatrix = RenderMath::Translate(RenderMath::Mat4(1.0f), m_Position) * RenderMath::ToMat4(orientation);
		m_ViewMatrix = RenderMath::Inverse(m_ViewMatrix);
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = (std::min)(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = (std::min)(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return {xFactor, yFactor};
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = (std::max)(distance, 0.0f);
		float speed = distance * distance;
		speed = (std::min)(speed, 100.0f); // max speed = 100
		return speed;
	}

	void EditorCamera::OnUpdate(Timestep ts)
	{
		if (Input::IsKeyPressed(Input::Key::LeftAlt))
		{
			const RenderMath::Vec2 &mouse{Input::GetMouseX(), Input::GetMouseY()};
			RenderMath::Vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
			m_InitialMousePosition = mouse;

			if (Input::IsMouseButtonPressed(Input::Mouse::ButtonMiddle))
				MousePan(delta);
			else if (Input::IsMouseButtonPressed(Input::Mouse::ButtonLeft))
				MouseRotate(delta);
			else if (Input::IsMouseButtonPressed(Input::Mouse::ButtonRight))
				MouseZoom(delta.y);
		}

		UpdateView();
	}

	void EditorCamera::OnEvent(Movement &e)
	{
		if (e.GetType() == MovementType::MouseScrolled)
		{
			OnMouseScroll(static_cast<MouseScrolled &>(e));
		}
	}

	bool EditorCamera::OnMouseScroll(MouseScrolled &e)
	{
		float delta = e.GetYOffset() * 0.1f;
		MouseZoom(delta);
		UpdateView();
		return false;
	}

	void EditorCamera::MousePan(const RenderMath::Vec2 &delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const RenderMath::Vec2 &delta)
	{
		float yawSign = (GetUpDirection().y < 0) ? -1.0f : 1.0f;
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed();
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForwardDirection();
			m_Distance = 1.0f;
		}
	}

	RenderMath::Vec3 EditorCamera::GetUpDirection() const
	{
		return GetOrientation() * RenderMath::Vec3(0.0f, 1.0f, 0.0f);
	}

	RenderMath::Vec3 EditorCamera::GetRightDirection() const
	{
		return GetOrientation() * RenderMath::Vec3(1.0f, 0.0f, 0.0f);
	}

	RenderMath::Vec3 EditorCamera::GetForwardDirection() const
	{
		return GetOrientation() * RenderMath::Vec3(0.0f, 0.0f, -1.0f);
	}

	RenderMath::Vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	RenderMath::Quat EditorCamera::GetOrientation() const
	{
		return RenderMath::Quat(RenderMath::Vec3(-m_Pitch, -m_Yaw, 0.0f));
	}
}
