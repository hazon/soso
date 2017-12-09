#include "ttydriver.h"
#include "device.h"
#include "screen.h"
#include "serial.h"
#include "devfs.h"
#include "alloc.h"
#include "common.h"

Tty* gTty1 = NULL;
Tty* gTty2 = NULL;
Tty* gTty3 = NULL;

Tty* gActiveTty = NULL;

static File* gKeyboard = NULL;

static uint8 gKeyModifier = 0;

#define gBufferSize 1024

typedef struct InputBuffer
{
    uint8 buffer[gBufferSize];
    uint32 bufferIndex;
} InputBuffer;

static InputBuffer* createInputBuffer()
{
    InputBuffer* inputBuffer = kmalloc(sizeof(InputBuffer));
    memset((uint8*)inputBuffer, 0, sizeof(InputBuffer));

    return inputBuffer;
}

typedef enum KeyModifier
{
    KM_LeftShift = 1,
    KM_RightShift = 2,
    KM_Ctrl = 4,
    KM_Alt = 8
} KeyModifier;

enum
{
    KEY_LEFTSHIFT = 0x2A,
    KEY_RIGHTSHIFT = 0x36,
    KEY_CTRL = 0x1D,
    KEY_ALT = 0x38,
    KEY_CAPSLOCK = 0x3A,
    KEY_F1 = 0x3B,
    KEY_F2 = 0x3C,
    KEY_F3 = 0x3D
};

// PC keyboard interface constants

#define KBSTATP         0x64    // kbd controller status port(I)
#define KBS_DIB         0x01    // kbd data in buffer
#define KBDATAP         0x60    // kbd data port(I)

#define NO              0

#define SHIFT           (1<<0)
#define CTL             (1<<1)
#define ALT             (1<<2)

#define CAPSLOCK        (1<<3)
#define NUMLOCK         (1<<4)
#define SCROLLLOCK      (1<<5)

#define E0ESC           (1<<6)

// Special keycodes
#define KEY_HOME        0xE0
#define KEY_END         0xE1
#define KEY_UP          0xE2
#define KEY_DN          0xE3
#define KEY_LF          0xE4
#define KEY_RT          0xE5
#define KEY_PGUP        0xE6
#define KEY_PGDN        0xE7
#define KEY_INS         0xE8
#define KEY_DEL         0xE9

// C('A') == Control-A
#define C(x) (x - '@')




static uint8 gKeyMap[256] =
{
  NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',  // 0x00
  '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 0x10
  'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 0x20
  '\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
  'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',  // 0x30
  NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
  NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
  '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
  [0x9C] = '\n',      // KP_Enter
  [0xB5] = '/',       // KP_Div
  [0xC8] = KEY_UP,    [0xD0] = KEY_DN,
  [0xC9] = KEY_PGUP,  [0xD1] = KEY_PGDN,
  [0xCB] = KEY_LF,    [0xCD] = KEY_RT,
  [0x97] = KEY_HOME,  [0xCF] = KEY_END,
  [0xD2] = KEY_INS,   [0xD3] = KEY_DEL
};

static uint8 gKeyShiftMap[256] =
{
  NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',  // 0x00
  '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 0x10
  'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 0x20
  '"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
  'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',  // 0x30
  NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
  NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
  '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
  [0x9C] = '\n',      // KP_Enter
  [0xB5] = '/',       // KP_Div
  [0xC8] = KEY_UP,    [0xD0] = KEY_DN,
  [0xC9] = KEY_PGUP,  [0xD1] = KEY_PGDN,
  [0xCB] = KEY_LF,    [0xCD] = KEY_RT,
  [0x97] = KEY_HOME,  [0xCF] = KEY_END,
  [0xD2] = KEY_INS,   [0xD3] = KEY_DEL
};

static BOOL tty_open(File *file, uint32 flags);
static void tty_close(File *file);
static int32 tty_read(File *file, uint32 size, uint8 *buffer);
static int32 tty_write(File *file, uint32 size, uint8 *buffer);

static uint8 getCharacterForScancode(KeyModifier modifier, uint8 scancode);
static void processScancode(uint8 scancode);

void initializeTTYs()
{
    gTty1 = createTty();
    gTty1->color = 0x0A;
    gTty1->privateData = createInputBuffer();

    gTty2 = createTty();
    gTty2->color = 0x0B;
    gTty2->privateData = createInputBuffer();

    gTty3 = createTty();
    gTty3->color = 0x0C;
    gTty3->privateData = createInputBuffer();

    gActiveTty = gTty1;
    Screen_CopyTo(gActiveTty->buffer);
    Screen_GetCursor(&(gActiveTty->currentLine), &(gActiveTty->currentColumn));
    Screen_ApplyColor(gActiveTty->color);

    //gBuffer = kmalloc(gBufferSize);

    FileSystemNode* keyboardNode = getFileSystemNode("/dev/keyboard");
    if (keyboardNode)
    {
        gKeyboard = open_fs(keyboardNode, 0);
    }

    {
        Device device;
        memset((uint8*)&device, 0, sizeof(Device));
        strcpy(device.name, "tty1");
        device.deviceType = FT_CharacterDevice;
        device.open = tty_open;
        device.close = tty_close;
        device.read = tty_read;
        device.write = tty_write;
        device.privateData = gTty1;
        registerDevice(&device);
    }

    {
        Device device;
        memset((uint8*)&device, 0, sizeof(Device));
        strcpy(device.name, "tty2");
        device.deviceType = FT_CharacterDevice;
        device.open = tty_open;
        device.close = tty_close;
        device.read = tty_read;
        device.write = tty_write;
        device.privateData = gTty2;
        registerDevice(&device);
    }

    {
        Device device;
        memset((uint8*)&device, 0, sizeof(Device));
        strcpy(device.name, "tty3");
        device.deviceType = FT_CharacterDevice;
        device.open = tty_open;
        device.close = tty_close;
        device.read = tty_read;
        device.write = tty_write;
        device.privateData = gTty3;
        registerDevice(&device);
    }
}

static BOOL tty_open(File *file, uint32 flags)
{
    return TRUE;
}

static void tty_close(File *file)
{
}

static int32 tty_read(File *file, uint32 size, uint8 *buffer)
{
    if (gKeyboard && size > 0)
    {
        uint8 scancode = 0;

        //Block until this becomes active TTY
        //while (file->node->privateData != gActiveTty);

        while (TRUE)
        {
            if (read_fs(gKeyboard, 1, &scancode) > 0)
            {
                processScancode(scancode);

                uint8 character = getCharacterForScancode(gKeyModifier, scancode);

                //Screen_PrintF("%d:%d\n", scancode, character);

                uint8 keyRelease = (0x80 & scancode); //ignore release event

                if (character > 0 && keyRelease == 0)
                {
                    if (gActiveTty == file->node->privateNodeData)
                    {
                        InputBuffer* inputBuffer = (InputBuffer*)gActiveTty->privateData;

                        if (inputBuffer->bufferIndex >= gBufferSize - 1)
                        {
                            inputBuffer->bufferIndex = 0;
                        }

                        if (character == '\b')
                        {
                            if (inputBuffer->bufferIndex > 0)
                            {
                                inputBuffer->buffer[--inputBuffer->bufferIndex] = '\0';

                                tty_write(file, 1, &character);
                            }
                        }
                        else
                        {
                            inputBuffer->buffer[inputBuffer->bufferIndex++] = character;

                            tty_write(file, 1, &character);
                        }



                        if (character == '\n')
                        {
                            int bytesToCopy = MIN(inputBuffer->bufferIndex, size);
                            memcpy(buffer, inputBuffer->buffer, bytesToCopy);
                            //Serial_PrintF("%d bytes. lastchar:%d\r\n", bytesToCopy, character);
                            inputBuffer->bufferIndex = 0;

                            return bytesToCopy;
                        }
                    }
                }
            }
        }
    }

    return -1;
}

static int32 tty_write(File *file, uint32 size, uint8 *buffer)
{
    buffer[size] = '\0';

    //Screen_PrintF("console_write\n");

    Tty_PrintF(file->node->privateNodeData, "%s", buffer);

    if (gActiveTty == file->node->privateNodeData)
    {
        Screen_PrintF("%s", buffer);
    }

    return size;
}

static void setActiveTty(Tty* tty)
{
    Screen_Clear();

    Screen_CopyFrom(tty->buffer);

    Screen_ApplyColor(tty->color);

    Screen_MoveCursor(tty->currentLine, tty->currentColumn);


    gActiveTty = tty;

    //Serial_PrintF("line:%d column:%d\r\n", gActiveTty->currentLine, gActiveTty->currentColumn);
}

static uint8 getCharacterForScancode(KeyModifier modifier, uint8 scancode)
{
    //return gKeyboardLayout[scancode];
    if ((modifier & KM_LeftShift) == KM_LeftShift || (modifier & KM_RightShift) == KM_RightShift)
    {
        return gKeyShiftMap[scancode];
    }

    return gKeyMap[scancode];
}

static void applyModifierKeys(KeyModifier modifier, uint8 scancode)
{
    if ((modifier & KM_Alt) == KM_Alt)
    {
        if (scancode == KEY_F1)
        {
            setActiveTty(gTty1);
        }
        else if (scancode == KEY_F2)
        {
            setActiveTty(gTty2);
        }
        else if (scancode == KEY_F3)
        {
            setActiveTty(gTty3);
        }
    }
}

static void processScancode(uint8 scancode)
{
    uint8 lastBit = scancode & 0x80;

    scancode &= 0x7F;

    if (lastBit)
    {
        //key release

        switch (scancode)
        {
        case KEY_LEFTSHIFT:
            gKeyModifier &= ~KM_LeftShift;
            break;
        case KEY_RIGHTSHIFT:
            gKeyModifier &= ~KM_RightShift;
            break;
        case KEY_CTRL:
            gKeyModifier &= ~KM_Ctrl;
            break;
        case KEY_ALT:
            gKeyModifier &= ~KM_Alt;
            break;
        }

        //Screen_PrintF("released: %x (%d)\n", scancode, scancode);
    }
    else
    {
        //key pressed

        switch (scancode)
        {
        case KEY_LEFTSHIFT:
            gKeyModifier |= KM_LeftShift;
            break;
        case KEY_RIGHTSHIFT:
            gKeyModifier |= KM_RightShift;
            break;
        case KEY_CTRL:
            gKeyModifier |= KM_Ctrl;
            break;
        case KEY_ALT:
            gKeyModifier |= KM_Alt;
            break;
        }

        //Screen_PrintF("pressed: %x (%d)\n", scancode, scancode);

        applyModifierKeys(gKeyModifier, scancode);
    }
}