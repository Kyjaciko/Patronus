#include "Camera3D.h"

Camera3D::Camera3D()
{
	m_Position		   = DirectX::XMFLOAT3(0.f, 0.f, 0.f);
	m_PositionVector = DirectX::XMVectorZero();

	m_Rotation		   = DirectX::XMFLOAT3(0.f, 0.f, 0.f);
	m_RotationVector = DirectX::XMVectorZero();
  
	UpdateMatrix();
}

Camera3D::Camera3D(float fovDegrees, float aspectRatio, float nearZ, float farZ)
	: Camera3D()
{
	this->SetProjectionValues(fovDegrees, aspectRatio, nearZ, farZ);
}

void Camera3D::SetProjectionValues(float fovDegrees, float aspectRatio, float nearZ, float farZ)
{
	const float fovRadians = DirectX::XMConvertToRadians(fovDegrees);
	m_ProjectionMatrix = DirectX::XMMatrixPerspectiveFovRH(fovRadians, aspectRatio, nearZ, farZ);
}

const DirectX::XMMATRIX& Camera3D::GetViewMatrix() const
{
	return m_ViewMatrix;
}

const DirectX::XMMATRIX& Camera3D::GetProjectionMatrix() const
{
	return m_ProjectionMatrix;
}

void Camera3D::UpdateMatrix()
{
	using namespace DirectX;

	const XMMATRIX rotation_matrix = XMMatrixRotationRollPitchYaw(m_Rotation.x, m_Rotation.y, m_Rotation.z);

	// Calculate the unit vector of the camera's target, based of the forward value transformend by the rotation matrix.
	XMVECTOR target = XMVector3TransformNormal(DEFAULT_FORWARD_VECTOR, rotation_matrix);
	target += m_PositionVector; // Adjust the camera's target to be offset by the camera's current position.

	// Calculate up direction based on current rotation.
	const XMVECTOR up = XMVector3TransformNormal(DEFAULT_UP_VECTOR, rotation_matrix);

	// Rebuild view matrix.
	m_ViewMatrix = XMMatrixLookAtRH(m_PositionVector, target, up);

	// Calculate forward, left, right and backward vectors.
	UpdateDirectionVectors();
}