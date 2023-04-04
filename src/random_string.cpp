#include "random_string.h"


char* randomString(int length)
{
   char* random_string = (char*)malloc(sizeof(char)*(length + 1));
   random_string[length] = '\0';
   for(int i = 0; i < length; i++)
   {
      int toss = random(3);
      if(toss == 0)
      {
         random_string[i] = random('a', 'z' + 1);
      }
      else if(toss == 1)
      {
         random_string[i] = random('A', 'Z' + 1);
      }
      else
      {
         random_string[i] = random('0', '9' + 1);
      }
   }
   Serial.println(random_string);
   return random_string;
}