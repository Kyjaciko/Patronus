#include "Keyboard.h"

Keyboard::Keyboard()
{
  // Initialize the key states to off.
  for (int i = 0; i < 256; i++)
    m_keyStates[i] = false;
}

template <typename T> 
T Keyboard::ReadQueue(std::queue<T>& queue)
{
  if (queue.empty()) return T();

  T event = queue.front();
  queue.pop();
  return event;
}

KeyboardEvent Keyboard::ReadKey()
{
  return ReadQueue(m_keyBuffer);
}

unsigned char Keyboard::ReadChar()
{
  return ReadQueue(m_charBuffer);
}

void Keyboard::OnKeyPress(const unsigned char key)
{
  m_keyStates[key] = true;
  m_keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::KEY_DOWN, key));
}

void Keyboard::OnKeyRelease(const unsigned char key)
{
  m_keyStates[key] = false;
  m_keyBuffer.push(KeyboardEvent(KeyboardEvent::EventType::KEY_UP, key));
}

void Keyboard::OnChar(const unsigned char key)
{
  m_charBuffer.push(key);
}

bool Keyboard::IsKeyPressed(const unsigned char key) const
{
  return m_keyStates[key];
}

bool Keyboard::IsKeyBufferEmpty() const
{
  return m_keyBuffer.empty();
}

bool Keyboard::IsCharBufferEmpty() const
{
  return m_charBuffer.empty();
}

bool Keyboard::AreKeysAutoRepeat() const
{
  return m_autoRepeatKeys;
}

bool Keyboard::AreCharsAutoRepeat() const
{
  return m_autoRepeatChars;
}

void Keyboard::EnableAutoRepeatKeys()
{
  m_autoRepeatKeys = true;
}

void Keyboard::DisableAutoRepeatKeys()
{
  m_autoRepeatKeys = false;
}

void Keyboard::EnableAutoRepeatChars()
{
  m_autoRepeatChars = true;
}

void Keyboard::DisableAutoRepeatChars()
{
  m_autoRepeatChars = false;
}