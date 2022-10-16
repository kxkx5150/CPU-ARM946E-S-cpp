
#include "input.h"
#include "core.h"


void Input::pressKey(int key)
{
    if (key < 10)
        keyInput &= ~BIT(key);
    else if (key < 12)
        extKeyIn &= ~BIT(key - 10);
}
void Input::releaseKey(int key)
{
    if (key < 10)
        keyInput |= BIT(key);
    else if (key < 12)
        extKeyIn |= BIT(key - 10);
}
void Input::pressScreen()
{
    extKeyIn &= ~BIT(6);
}
void Input::releaseScreen()
{
    extKeyIn |= BIT(6);
}
