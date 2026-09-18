#pragma once

#include <queue>
#include "../../core/stdafx.h"
#include "KeyboardEvent.h"

class Keyboard
{
public:
	Keyboard();

	KeyboardEvent ReadKey();
	unsigned char ReadChar();

	void OnKeyPress(const unsigned char key);
	void OnKeyRelease(const unsigned char key);
	void OnChar(const unsigned char key);

	bool IsKeyPressed(const unsigned char key) const;
	bool IsKeyBufferEmpty() const;
	bool IsCharBufferEmpty() const;
	bool AreKeysAutoRepeat() const;
	bool AreCharsAutoRepeat() const;

	void EnableAutoRepeatKeys();
	void DisableAutoRepeatKeys();
	void EnableAutoRepeatChars();
	void DisableAutoRepeatChars();

private:
	bool m_autoRepeatKeys  = false;
	bool m_autoRepeatChars = false;

	bool m_keyStates[256];

	std::queue<KeyboardEvent> m_keyBuffer;
	std::queue<unsigned char> m_charBuffer;

	template <typename T> 
	T ReadQueue(std::queue<T>& queue);
};