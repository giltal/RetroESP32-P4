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
// $Id: Serializer.cxx 2838 2014-01-17 23:34:03Z stephena $
//============================================================================

// ESP32-P4: Rewritten to use C FILE* I/O instead of fstream/iostream
// to avoid pulling in ~123KB of C++ stream library into DRAM.

#include "Serializer.hxx"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// -- helpers for memory-based mode --
void Serializer::memEnsure(uInt32 extra)
{
  uInt32 need = myBufPos + extra;
  if (need <= myBufCapacity) return;
  uInt32 newCap = myBufCapacity ? myBufCapacity * 2 : 4096;
  if (newCap < need) newCap = need;
  myBuf = (uInt8*)realloc(myBuf, newCap);
  myBufCapacity = newCap;
}

void Serializer::memWrite(const void* data, uInt32 size)
{
  memEnsure(size);
  memcpy(myBuf + myBufPos, data, size);
  myBufPos += size;
  if (myBufPos > myBufSize) myBufSize = myBufPos;
}

void Serializer::memRead(void* data, uInt32 size)
{
  if (myBufPos + size > myBufSize) {
    memset(data, 0, size);
    return;
  }
  memcpy(data, myBuf + myBufPos, size);
  myBufPos += size;
}

// -- File-based constructor --
Serializer::Serializer(const string& filename, bool readonly)
  : myFile(NULL), myUseFile(true), myValid(false),
    myBuf(NULL), myBufSize(0), myBufCapacity(0), myBufPos(0)
{
  if (readonly) {
    myFile = fopen(filename.c_str(), "rb");
  } else {
    // Create file if it doesn't exist (append mode creates it)
    FILE* tmp = fopen(filename.c_str(), "ab");
    if (tmp) fclose(tmp);
    myFile = fopen(filename.c_str(), "r+b");
  }
  myValid = (myFile != NULL);
}

// -- Memory-based constructor --
Serializer::Serializer(void)
  : myFile(NULL), myUseFile(false), myValid(true),
    myBuf(NULL), myBufSize(0), myBufCapacity(0), myBufPos(0)
{
  // Pre-allocate some space and write initial bool (matches original)
  memEnsure(4096);
  putBool(true);
  reset();
}

// -- Destructor --
Serializer::~Serializer(void)
{
  if (myFile) { fclose(myFile); myFile = NULL; }
  if (myBuf)  { free(myBuf); myBuf = NULL; }
}

bool Serializer::isValid(void) { return myValid; }

void Serializer::reset(void)
{
  if (myUseFile && myFile)
    fseek(myFile, 0, SEEK_SET);
  else
    myBufPos = 0;
}

// ---- get methods ----
uInt8 Serializer::getByte(void)
{
  uInt8 val = 0;
  if (myUseFile) fread(&val, 1, 1, myFile);
  else           memRead(&val, 1);
  return val;
}

void Serializer::getByteArray(uInt8* array, uInt32 size)
{
  if (myUseFile) fread(array, 1, size, myFile);
  else           memRead(array, size);
}

uInt16 Serializer::getShort(void)
{
  uInt16 val = 0;
  if (myUseFile) fread(&val, 1, sizeof(uInt16), myFile);
  else           memRead(&val, sizeof(uInt16));
  return val;
}

void Serializer::getShortArray(uInt16* array, uInt32 size)
{
  uInt32 bytes = sizeof(uInt16) * size;
  if (myUseFile) fread(array, 1, bytes, myFile);
  else           memRead(array, bytes);
}

uInt32 Serializer::getInt(void)
{
  uInt32 val = 0;
  if (myUseFile) fread(&val, 1, sizeof(uInt32), myFile);
  else           memRead(&val, sizeof(uInt32));
  return val;
}

void Serializer::getIntArray(uInt32* array, uInt32 size)
{
  uInt32 bytes = sizeof(uInt32) * size;
  if (myUseFile) fread(array, 1, bytes, myFile);
  else           memRead(array, bytes);
}

string Serializer::getString(void)
{
  int len = getInt();
  string str;
  str.resize(len);
  if (myUseFile) fread(&str[0], 1, len, myFile);
  else           memRead(&str[0], len);
  return str;
}

bool Serializer::getBool(void)
{
  return getByte() == TruePattern;
}

// ---- put methods ----
void Serializer::putByte(uInt8 value)
{
  if (myUseFile) fwrite(&value, 1, 1, myFile);
  else           memWrite(&value, 1);
}

void Serializer::putByteArray(const uInt8* array, uInt32 size)
{
  if (myUseFile) fwrite(array, 1, size, myFile);
  else           memWrite(array, size);
}

void Serializer::putShort(uInt16 value)
{
  if (myUseFile) fwrite(&value, 1, sizeof(uInt16), myFile);
  else           memWrite(&value, sizeof(uInt16));
}

void Serializer::putShortArray(const uInt16* array, uInt32 size)
{
  uInt32 bytes = sizeof(uInt16) * size;
  if (myUseFile) fwrite(array, 1, bytes, myFile);
  else           memWrite(array, bytes);
}

void Serializer::putInt(uInt32 value)
{
  if (myUseFile) fwrite(&value, 1, sizeof(uInt32), myFile);
  else           memWrite(&value, sizeof(uInt32));
}

void Serializer::putIntArray(const uInt32* array, uInt32 size)
{
  uInt32 bytes = sizeof(uInt32) * size;
  if (myUseFile) fwrite(array, 1, bytes, myFile);
  else           memWrite(array, bytes);
}

void Serializer::putString(const string& str)
{
  int len = str.length();
  putInt(len);
  if (myUseFile) fwrite(str.data(), 1, len, myFile);
  else           memWrite(str.data(), len);
}

void Serializer::putBool(bool b)
{
  putByte(b ? TruePattern : FalsePattern);
}

// ---- Memory buffer access ----
string Serializer::get()
{
  if (myUseFile) return string();
  return string((char*)myBuf, myBufSize);
}

void Serializer::set(const string& data)
{
  if (myUseFile) return;
  myBufSize = data.size();
  memEnsure(myBufSize);
  memcpy(myBuf, data.data(), myBufSize);
  myBufPos = 0;
}
