/*
  Copyright (c) 2016, Marko Viitanen (Fador)

  Permission to use, copy, modify, and/or distribute this software for any purpose 
  with or without fee is hereby granted, provided that the above copyright notice 
  and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH 
  REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, 
  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
  LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
  OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR 
  PERFORMANCE OF THIS SOFTWARE.

*/

#include <Windows.h>
#include <iostream>
#include <string>
#include "tap-windows.h"

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

// wide char to multi byte:
std::string ws2s(const std::wstring& wstr)
{
    int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), int(wstr.length() ), 0, 0, 0, 0); 
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), int(wstr.length() ), &strTo[0], size_needed, 0, 0); 
    return strTo;
}

std::string getGuid()
{
  HKEY hKey;
  long ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, ADAPTER_KEY, 0, KEY_ALL_ACCESS, &hKey);
  if (ret != ERROR_SUCCESS) {
    return "";
  }
  TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
  DWORD    cbName;                   // size of name string 
  TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
  DWORD    cchClassName = MAX_PATH;  // size of class string 
  DWORD    cSubKeys=0;               // number of subkeys 
  DWORD    cbMaxSubKey;              // longest subkey size 
  DWORD    cchMaxClass;              // longest class string 
  DWORD    cValues;              // number of values for key 
  DWORD    cchMaxValue;          // longest value name 
  DWORD    cbMaxValueData;       // longest value data 
  DWORD    cbSecurityDescriptor; // size of security descriptor 
  FILETIME ftLastWriteTime;      // last write time
  std::string guid;
  DWORD i, retCode; 
  void* pData = malloc(MAX_VALUE_NAME);
  TCHAR  achValue[MAX_VALUE_NAME]; 
  DWORD cchValue = MAX_VALUE_NAME; 
 
  // Get the class name and the value count. 
  retCode = RegQueryInfoKey(
      hKey,                    // key handle 
      achClass,                // buffer for class name 
      &cchClassName,           // size of class string 
      NULL,                    // reserved 
      &cSubKeys,               // number of subkeys 
      &cbMaxSubKey,            // longest subkey size 
      &cchMaxClass,            // longest class string 
      &cValues,                // number of values for this key 
      &cchMaxValue,            // longest value name 
      &cbMaxValueData,         // longest value data 
      &cbSecurityDescriptor,   // security descriptor 
      &ftLastWriteTime);       // last write time 
 
  // Enumerate the subkeys, until RegEnumKeyEx fails.
  bool correctDevice = false;
  if (cSubKeys) {
    for (i=0; i<cSubKeys; i++) { 
      cbName = MAX_KEY_LENGTH;
      retCode = RegEnumKeyEx(hKey, i, achKey, &cbName, NULL, NULL, NULL, &ftLastWriteTime); 
      if (retCode == ERROR_SUCCESS) {

        HKEY subKey;
        std::wstring key = std::wstring(ADAPTER_KEY) + L"\\" + std::wstring(achKey);
        //std::cout << ws2s(std::wstring(achKey)) << std::endl;
        
        long ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_ALL_ACCESS, &subKey);

        if (ret == ERROR_SUCCESS) {
          DWORD    subSubKeys=0;
          retCode = RegQueryInfoKey(subKey,achClass,&cchClassName,NULL,&subSubKeys,
              &cbMaxSubKey, &cchMaxClass, &cValues, &cchMaxValue,&cbMaxValueData,
              &cbSecurityDescriptor,&ftLastWriteTime);
          if (cValues && retCode==ERROR_SUCCESS) {
            correctDevice = false;
            for (int ii=0; ii<cValues; ii++) { 
              cchValue = MAX_VALUE_NAME;
              achValue[0] = '\0';
              DWORD  len=MAX_VALUE_NAME;
              retCode = RegEnumValue(subKey, ii, achValue,&cchValue, NULL, NULL,(LPBYTE)pData, &len);
              if (retCode == ERROR_SUCCESS) {
                //std::cout << "Values: " << ws2s(std::wstring(achValue)) << ": " << ws2s(std::wstring((LPTSTR)pData)) << std::endl;
                std::string val = ws2s(std::wstring(achValue));
                std::string data = ws2s(std::wstring((LPTSTR)pData));
                if (val == "ComponentId" && data == "tap0901") { 
                  correctDevice = true;
                }
                if (val == "NetCfgInstanceId") { 
                  guid = data;
                } 
              }
            }
            if (correctDevice) break;
          }
        }
        RegCloseKey(subKey);
      }
      if (correctDevice) break;    
    }
  } 


  RegCloseKey(hKey);

  if (correctDevice) return guid;
  free(pData);
  return "";
}