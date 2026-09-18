#include "KeyboardEvent.h"

KeyboardEvent::KeyboardEvent()
  : m_type(EventType::INVALID)
  , m_key(0u)
{
}

KeyboardEvent::KeyboardEvent(const EventType type, const unsigned char key)
  : m_type(type)
  , m_key(key)
{
}

bool KeyboardEvent::IsPressed() const
{
  return m_type == EventType::KEY_DOWN;
}

bool KeyboardEvent::IsReleased() const
{
  return m_type == EventType::KEY_UP;
}

bool KeyboardEvent::IsValid() const
{
  return m_type != EventType::INVALID;
}

unsigned char KeyboardEvent::GetKey() const
{
  return m_key;
}