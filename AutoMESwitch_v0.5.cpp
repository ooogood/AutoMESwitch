#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "taiwanKeyGroupDefine.h"
// intro:
// use tone keys( group no.4 ) as seperate point, generate 'sections'
// record the section sizes in this buffer
// ex. 134 => LLKP_SecSizeRecord[ 2 ]++
// highly possible Taiwanese input section size: 2, 3
// highly possible English input sectoin size: MaxBufferSize
// highly possible number input section size: 0
// ambiguous input section size: 1, 6~12
// another highly possible English input is more than 3 consecutive key in the same group
// todo: i can use AI to train a decision maker 
// version 0.5 by Joseph Chen 17/11/2021
const static unsigned int LLKP_KEYBUFFERSIZE = 12;
static DWORD LLKP_KeyBuffer[ LLKP_KEYBUFFERSIZE ] = { 0 };
static unsigned int LLKP_KeyCntInBuffer = 0;
static unsigned int LLKP_SecSizeRecord[ LLKP_KEYBUFFERSIZE ] = { 0 };
static unsigned int LLKP_SecCnt = 0;
static unsigned int LLKP_SecSizeTmp = 0;
static unsigned int LLKP_lastKeyGrp = 0;
static unsigned int LLKP_consecSameGrpCnt = 0;
static boolean LLKP_bProcLock = false;
static unsigned int LLKP_KEYGROUPMAP[ 0xFF ] = { 0 };
static HWND LLKP_lastHWnd = NULL;

// initialize key group map
static void LLKP_Init( void ) {
	for( DWORD i : LLKP_KEYGROUPUPPER ) {
        LLKP_KEYGROUPMAP[ i ] = 1;
    }
	for( DWORD i : LLKP_KEYGROUPMIDDLE ) {
        LLKP_KEYGROUPMAP[ i ] = 2;
    }
	for( DWORD i : LLKP_KEYGROUPLOWER ) {
        LLKP_KEYGROUPMAP[ i ] = 3;
    }
	for( DWORD i : LLKP_KEYGROUPTONE ) {
        LLKP_KEYGROUPMAP[ i ] = 4;
    }
    // space is a special key
    LLKP_KEYGROUPMAP[ 0x20 ] = 5;
}

// clear buffer and clear count
static void LLKP_ClearBuffers() {
    memset( LLKP_KeyBuffer, 0, sizeof( DWORD ) * LLKP_KEYBUFFERSIZE );
    LLKP_KeyCntInBuffer = 0;
    memset( LLKP_SecSizeRecord, 0, sizeof( DWORD ) * LLKP_KEYBUFFERSIZE );
    LLKP_SecCnt = 0;
    LLKP_SecSizeTmp = 0;
    LLKP_lastKeyGrp = 0;
    LLKP_consecSameGrpCnt = 0;
}

// calculate backspace count needed to correct content in buffer in Taiwanese
static unsigned int LLKP_CalcTWBackspaceCnt( void ) {
    unsigned int ret = LLKP_SecCnt;
    unsigned int one = 0, two = 0, three = 0;
    for( int i = LLKP_KeyCntInBuffer - 1; i >= 0; --i ) {
        switch( LLKP_KEYGROUPMAP[ LLKP_KeyBuffer[ i ] ] ) {
        case 4:
            return ret + one + two + three;
            break;
        case 1:
            one = 1;
            break;
        case 2:
            two = 1;
            break;
        case 3:
            three = 1;
            break;
        default:
            break;
        }
    }
    return ret + one + two + three;
}

// resend keyboard input in the key buffer
// normally call this method after changing keyboard layout
static void LLKP_ReviseQuedKBInput( LPCSTR language_select ) {
    unsigned int backspaceCnt = 0;
    // for now we assume there are only two KBLs, Taiwanese and English in this computer
    // I always have only two :)
    // todo: optimize delete count( maybe I should use more lock? )
    // delete wrong input in English
    if( language_select == "00000404" ) {
        backspaceCnt = LLKP_KeyCntInBuffer;
    }
    // delete wrong input in Taiwanese
    else {
        backspaceCnt = LLKP_CalcTWBackspaceCnt();
    }

    // resend queued input
    INPUT bsInput[ LLKP_KEYBUFFERSIZE ];
    int i = 0;
    for(; i < backspaceCnt; ++i ) {
        bsInput[ i ].type = INPUT_KEYBOARD;
        bsInput[ i ].ki.wVk = (WORD)VK_BACK;
        bsInput[ i ].ki.dwFlags = KEYEVENTF_UNICODE;
    }
    SendInput( backspaceCnt, bsInput, sizeof( INPUT ) );
    INPUT inputs[ LLKP_KEYBUFFERSIZE ];
    // don't send the last one because the key is still going through the KB chain
    for(int i = 0; i < LLKP_KeyCntInBuffer; ++i ) {
        inputs[ i ].type = INPUT_KEYBOARD;
        inputs[ i ].ki.wVk = LLKP_KeyBuffer[ i ];
        inputs[ i ].ki.dwFlags = KEYEVENTF_UNICODE;
    }
    SendInput( LLKP_KeyCntInBuffer, inputs, sizeof( INPUT ) );
}

// switch keyboard layout
static void LLKP_SwitchKBLayout( LPCSTR language_select ) {
    // "00000409" is code for US-English
    // "00000404" is code for Chinese-Taiwan
    HWND curWnd = GetForegroundWindow();
    HKL curKBL = GetKeyboardLayout( GetWindowThreadProcessId( curWnd, nullptr ) );
    if( language_select == "00000409" && curKBL == (HKL)0x4090409 ) return; 
    if( language_select == "00000404" && curKBL == (HKL)0x4040404 ) return; 
    SendMessageA( curWnd, WM_INPUTLANGCHANGEREQUEST, 0,
         reinterpret_cast<LPARAM>( LoadKeyboardLayoutA( language_select, KLF_ACTIVATE ) ) );
    LLKP_ReviseQuedKBInput( language_select );
}

// determine user KBL according to buffer data
// todo: optimize this
LPCSTR LLKP_DetermineKBL() {
    if( LLKP_SecCnt == 0 || LLKP_SecSizeRecord[ 0 ] > 1 ) {
        return "00000409";
    }
    else if( LLKP_SecSizeRecord[ 1 ] + LLKP_SecSizeRecord[ 2 ] + LLKP_SecSizeRecord[ 3 ] > 2 ) {
        return "00000404";
    }
    else return "0";
}

// single input key router
static void AutoMESwitch( DWORD nKey ) {
    unsigned int nKeyGroup = LLKP_KEYGROUPMAP[ nKey ];
    switch( nKeyGroup ) {
    case 0:
        // return key
        // highly possible to be a Taiwanese input
        if( nKey == 0x0D ) {
            LLKP_ClearBuffers();
        }
        // backspace key
        if( nKey == 0x08 ) {
            if( LLKP_KeyCntInBuffer > 0 )
                --LLKP_KeyCntInBuffer;
            LLKP_KeyBuffer[ LLKP_KeyCntInBuffer ] = 0;
            if( LLKP_SecSizeTmp > 0 )
                --LLKP_SecSizeTmp;
        }
        break;
    case 4:
        // put key pattern into buffers
        LLKP_KeyBuffer[ LLKP_KeyCntInBuffer++ ] = nKey;
        ++LLKP_SecSizeRecord[ LLKP_SecSizeTmp ];
        ++LLKP_SecCnt;
        LLKP_SecSizeTmp = 0;
        LLKP_lastKeyGrp = 0;
        LLKP_consecSameGrpCnt = 0;
        break;
    default:
        // put key pattern into buffers
        ++LLKP_SecSizeTmp;
        LLKP_KeyBuffer[ LLKP_KeyCntInBuffer++ ] = nKey;
        if( nKeyGroup != 5 && nKeyGroup == LLKP_lastKeyGrp ) {
            // more than 3 consecutive number in the same group 
            // highly possible to be english or number input
            if( ++LLKP_consecSameGrpCnt > 3 ) {
                LPCSTR KBL = LLKP_DetermineKBL();
                if( KBL != "0" ) {
                    LLKP_bProcLock = true;
                    LLKP_SwitchKBLayout( KBL );
                    LLKP_ClearBuffers();
                    LLKP_bProcLock = false;
                    return;
                }
            }
        }
        else {
            LLKP_consecSameGrpCnt = 0;
            LLKP_lastKeyGrp = nKeyGroup;
        }

        if( LLKP_KeyCntInBuffer >= LLKP_KEYBUFFERSIZE ) {
            LPCSTR KBL = LLKP_DetermineKBL();
            if( KBL != "0" ) {
                LLKP_bProcLock = true;
                LLKP_SwitchKBLayout( KBL );
            }
            LLKP_ClearBuffers();
            LLKP_bProcLock = false;
        }
        break;
    }
}

// hook procedure
static LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam )
{
    if ( !LLKP_bProcLock && ( wParam == WM_KEYUP ) && lParam != NULL ) {
        AutoMESwitch( ( (LPKBDLLHOOKSTRUCT)lParam )->vkCode );
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main()
{
    LLKP_Init();
    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    // it works if I use MessageBox to make it hold
    MessageBoxW(NULL, L"AutoMESwitch is running.", L"AutoMESwitch", MB_OK );
    UnhookWindowsHookEx(hHook);
    return 0;
}