//============================================================================
//
//   SSSS    tt          lll  lll       
//  SS  SS   tt           ll   ll        
//  SS     tttttt  eeee   ll   ll   aaaa 
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2014 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//
// $Id: Serializer.hxx 2838 2014-01-17 23:34:03Z stephena $
//============================================================================

#ifndef SERIALIZER_HXX
#define SERIALIZER_HXX

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "bspf.hxx"

/**
  Serializer — rewritten for ESP32-P4 using C FILE* I/O
  to avoid iostream/fstream DRAM overhead (~123KB).
*/
class Serializer
{
  public:
    Serializer(const string& filename, bool readonly = false);
    Serializer(void);
    virtual ~Serializer(void);

  public:
    bool isValid(void);
    void reset(void);

    uInt8  getByte(void);
    void   getByteArray(uInt8* array, uInt32 size);
    uInt16 getShort(void);
    void   getShortArray(uInt16* array, uInt32 size);
    uInt32 getInt(void);
    void   getIntArray(uInt32* array, uInt32 size);
    string getString(void);
    bool   getBool(void);

    void putByte(uInt8 value);
    void putByteArray(const uInt8* array, uInt32 size);
    void putShort(uInt16 value);
    void putShortArray(const uInt16* array, uInt32 size);
    void putInt(uInt32 value);
    void putIntArray(const uInt32* array, uInt32 size);
    void putString(const string& str);
    void putBool(bool b);

    /* In-memory buffer access (for StateManager) */
    string get();
    void set(const string& data);

  private:
    /* File-based mode */
    FILE* myFile;
    bool  myUseFile;
    bool  myValid;

    /* Memory-based mode */
    uInt8* myBuf;
    uInt32 myBufSize;
    uInt32 myBufCapacity;
    uInt32 myBufPos;

    void memWrite(const void* data, uInt32 size);
    void memRead(void* data, uInt32 size);
    void memEnsure(uInt32 extra);

    enum {
      TruePattern  = 0xfe,
      FalsePattern = 0x01
    };
};

#endif
