#ifndef DESKTOPENVIRONMENT_H
#define DESKTOPENVIRONMENT_H

#include "common.h"
#include "process.h"
#include "list.h"

typedef struct DesktopEnvironment DesktopEnvironment;

typedef struct Window Window;

DesktopEnvironment* DE_Create(uint16 width, uint16 height);
DesktopEnvironment* DE_GetDefault();
void DE_SetDefault(DesktopEnvironment* de);
uint16 DE_GetWidth(DesktopEnvironment* de);
uint16 DE_GetHeight(DesktopEnvironment* de);
void DE_Update(DesktopEnvironment* de);
Window* DE_CreateWindow(DesktopEnvironment* de, uint16 width, uint16 height, Thread* ownerThread);
void DE_DestroyWindow(Window* window);

#endif // DESKTOPENVIRONMENT_H
