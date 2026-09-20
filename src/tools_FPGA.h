#ifndef _TOOLS_H_
#define _TOOLS_H_

#include <stdint.h>
#include <string>
#include <vector>
#include <iostream>
#include <exception>

#if _MSC_VER >=1800 // vc12
#include <filesystem> // for std::tr2::sys::path
#elif defined __GNUC__ // gcc
#include <sys/types.h>
#include <dirent.h>
#else
#endif


#ifndef uchar
typedef unsigned char uchar;
#endif

#ifndef ushort
typedef unsigned short ushort;
#endif

#ifndef uint
typedef unsigned int uint;
#endif

#ifndef ulong
typedef unsigned long ulong;
#endif

#ifndef ullong
typedef unsigned long long ullong;
#endif

namespace tools
{
   class exception : public std::exception {
   private:
      std::string mess;

   public:
      exception(const char *src, const char *fmt, ...) throw();
      ~exception() throw();
      const char * what() const throw();
   };

   bool isExist(const std::string& file);

   template<typename T>
   std::string ttos(T val, int size);

   std::string shtos (short  val, int size=-1);
   std::string ushtos(ushort val, int size=-1);
   std::string itos  (int    val, int size=-1);
   std::string uitos (uint   val, int size=-1);
   std::string ltos  (long   val, int size=-1);
   std::string ultos (ulong  val, int size=-1);

   std::string ftos(float val, int prec=-1);

   uint32_t swapBit(uint32_t x, short b);
   bool isMatch(const char *str1, const char *str2);
   bool isMatchExtension(const std::string& file, const char *ext);
   int isMatchW(const char *wildcard, const char *target);

   int substr(char *t, const char *s, int pos, int len);

#if (_MSC_VER >=1800) || (defined __GNUC__) // vc12 or gcc
   static std::vector<std::string> fileNames;
   static std::vector<std::string> dirNames;
   void search(const std::string &pathName, bool clear=true);
   std::vector<std::string> getFileNames(const std::string &pathName, bool clear=true);
   std::vector<std::string> getDirNames(const std::string &pathName, bool clear=true);
   std::vector<std::string> getMatchFileNames(const std::string &pathName, const char *wildcard);
#endif

   std::vector<std::string> getSplitNames(const std::string &str, char delim);
   std::string getFirstName(const std::string &str, char delim);
   std::string getLastName(const std::string &str, char delim);
}

#endif
