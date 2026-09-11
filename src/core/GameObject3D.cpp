#include "GameObject3D.h"

void GameObject3D::SetLookAtPosition(DirectX::XMFLOAT3 lookAtPosition)
{
	// Verify that the lookAtPostion is not the same as the camera position. 
	// They cannot be the same as that wouldn't make sense and would result in undefined behavior.
	if (lookAtPosition.x == m_Position.x && lookAtPosition.y == m_Position.y && lookAtPosition.z == m_Position.z)
		return;

	lookAtPosition.x -= m_Position.x;
	lookAtPosition.y -= m_Position.y;
	lookAtPosition.z -= m_Position.z;

	const float distance = std::sqrt(lookAtPosition.x * lookAtPosition.x + lookAtPosition.z * lookAtPosition.z);
	const float roll = m_Rotation.z;
	const float pitch = std::atan2(lookAtPosition.y, distance);
	const float yaw = std::atan2(lookAtPosition.x, -lookAtPosition.z);

	SetRotation(pitch, yaw, roll);
}

const DirectX::XMVECTOR& GameObject3D::GetForwardVector(bool omitY) const
{
	return omitY ? m_ForwardVector_NO_Y : m_ForwardVector;
}

const DirectX::XMVECTOR& GameObject3D::GetLeftVector(bool omitY) const
{
	return omitY ? m_LeftVector_NO_Y : m_LeftVector;
}

const DirectX::XMVECTOR& GameObject3D::GetRightVector(bool omitY) const
{
	return omitY ? m_RightVector_NO_Y : m_RightVector;
}

const DirectX::XMVECTOR& GameObject3D::GetBackwardVector(bool omitY) const
{
	return omitY ? m_BackwardVector_NO_Y : m_BackwardVector;
}

const DirectX::XMVECTOR& GameObject3D::GetUpVector() const
{
	return m_UpVector;
}

void GameObject3D::UpdateDirectionVectors()
{
	// Include pitch, yaw and roll.
	const DirectX::XMMATRIX vector_rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(m_Rotation.x, m_Rotation.y, m_Rotation.z);

	m_ForwardVector  = DirectX::XMVector3TransformNormal(DEFAULT_FORWARD_VECTOR, vector_rotation_matrix);
	m_BackwardVector = DirectX::XMVector3TransformNormal(DEFAULT_BACKWARD_VECTOR, vector_rotation_matrix);

	m_LeftVector  = DirectX::XMVector3TransformNormal(DEFAULT_LEFT_VECTOR, vector_rotation_matrix);
	m_RightVector	= DirectX::XMVector3TransformNormal(DEFAULT_RIGHT_VECTOR, vector_rotation_matrix);

	m_UpVector = DirectX::XMVector3TransformNormal(DEFAULT_UP_VECTOR, vector_rotation_matrix);

  // Exclude pitch and roll: horizontal movement only.
	const DirectX::XMMATRIX vector_rotation_matrix_NO_Y = DirectX::XMMatrixRotationRollPitchYaw(0.f, m_Rotation.y, 0.f);

	m_ForwardVector_NO_Y	= DirectX::XMVector3TransformNormal(DEFAULT_FORWARD_VECTOR, vector_rotation_matrix_NO_Y);
	m_BackwardVector_NO_Y	= DirectX::XMVector3TransformNormal(DEFAULT_BACKWARD_VECTOR, vector_rotation_matrix_NO_Y);

	m_LeftVector_NO_Y	 = DirectX::XMVector3TransformNormal(DEFAULT_LEFT_VECTOR, vector_rotation_matrix_NO_Y);
	m_RightVector_NO_Y = DirectX::XMVector3TransformNormal(DEFAULT_RIGHT_VECTOR, vector_rotation_matrix_NO_Y);
}