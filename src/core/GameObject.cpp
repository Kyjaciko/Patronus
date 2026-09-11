#include "GameObject.h"

void GameObject::SetPosition(const DirectX::XMVECTOR& position)
{
	m_PositionVector = position;
	XMStoreFloat3(&m_Position, m_PositionVector);
	UpdateMatrix();
}

void GameObject::SetPosition(const DirectX::XMFLOAT3& position)
{
	m_Position = position;
	m_PositionVector = XMLoadFloat3(&m_Position);
	UpdateMatrix();
}

void GameObject::SetPosition(float x, float y, float z)
{
	m_Position = DirectX::XMFLOAT3(x, y, z);
	m_PositionVector = XMLoadFloat3(&m_Position);
	UpdateMatrix();
}

void GameObject::AdjustPosition(const DirectX::XMVECTOR& position)
{
	using namespace DirectX;

	m_PositionVector += position;
	XMStoreFloat3(&m_Position, m_PositionVector);
	UpdateMatrix();
}

void GameObject::AdjustPosition(const DirectX::XMFLOAT3& position)
{
	m_Position.x += position.x;
	m_Position.y += position.y;
	m_Position.z += position.z;
	m_PositionVector = XMLoadFloat3(&m_Position);
	UpdateMatrix();
}

void GameObject::AdjustPosition(float x, float y, float z)
{
	m_Position.x += x;
	m_Position.y += y;
	m_Position.z += z;
	m_PositionVector = XMLoadFloat3(&m_Position);
	UpdateMatrix();
}

void GameObject::SetRotation(const DirectX::XMVECTOR& rotation)
{
	m_RotationVector = rotation;
	XMStoreFloat3(&m_Rotation, m_RotationVector);
	UpdateMatrix();
}

void GameObject::SetRotation(const DirectX::XMFLOAT3& rotation)
{
	m_Rotation = rotation;
	m_RotationVector = XMLoadFloat3(&m_Rotation);
	UpdateMatrix();
}

void GameObject::SetRotation(float x, float y, float z)
{
	m_Rotation = DirectX::XMFLOAT3(x, y, z);
	m_RotationVector = XMLoadFloat3(&m_Rotation);
	UpdateMatrix();
}

void GameObject::AdjustRotation(const DirectX::XMVECTOR& rotation)
{
	using namespace DirectX;

	m_RotationVector += rotation;
	XMStoreFloat3(&m_Rotation, m_RotationVector);
	UpdateMatrix();
}

void GameObject::AdjustRotation(const DirectX::XMFLOAT3& rotation)
{
	m_Rotation.x += rotation.x;
	m_Rotation.y += rotation.y;
	m_Rotation.z += rotation.z;
	m_RotationVector = XMLoadFloat3(&m_Rotation);
	UpdateMatrix();
}

void GameObject::AdjustRotation(float x, float y, float z)
{
	m_Rotation.x += x;
	m_Rotation.y += y;
	m_Rotation.z += z;
	m_RotationVector = XMLoadFloat3(&m_Rotation);
	UpdateMatrix();
}

void GameObject::SetScale(float xScale, float yScale, float zScale)
{
	m_Scale.x = xScale;
	m_Scale.y = yScale;
	m_Scale.z = zScale;
	UpdateMatrix();
}

const DirectX::XMVECTOR& GameObject::GetPositionVector() const
{
	return m_PositionVector;
}

const DirectX::XMFLOAT3& GameObject::GetPositionFloat3() const
{
	return m_Position;
}

const DirectX::XMVECTOR& GameObject::GetRotationVector() const
{
  return m_RotationVector;
}

const DirectX::XMFLOAT3& GameObject::GetRotationFloat3() const
{
  return m_Rotation;
}