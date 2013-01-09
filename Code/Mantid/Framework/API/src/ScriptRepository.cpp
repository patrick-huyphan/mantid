#include "MantidAPI/ScriptRepository.h"
#ifdef _WIN32
#define snprintf _snprintf
#endif
namespace Mantid
{
  namespace API
  {
           
    ScriptRepoException::ScriptRepoException( int err_, 
                                              const std::string info,
                                              const std::string file,
                                              int line )
    {
      if (err_)        
        _system_error = strerror(err_); 
      else{
        _system_error = "Unknown";
      }
      
      if (info.empty())
        _user_info = _system_error; 
      else
        _user_info = info;                 

      if (file.empty()){
        _file_path = "Not provided"; 
      }else{
        _file_path = file; 
        if (line > 0){
          char aux[10]; 
          snprintf(aux, 10, ":%d",line); 
          _file_path.append(aux); 
        }
      }
      snprintf(general_info, 300, "%s\n%s\n",_user_info.c_str(),_system_error.c_str()); 
    }

    ScriptRepoException::ScriptRepoException( const std::string info,  
                                              const std::string system,
                                              const std::string file ,
                                              int line)
    {
        _system_error = system; 
        _user_info = info; 

        if (file.empty()){
          _file_path = "Not provided"; 
        }else{
          _file_path = file; 
          if (line > 0){
          char aux[10]; 
          snprintf(aux, 10, ":%d",line); 
          _file_path.append(aux); 
          }
        }
      snprintf(general_info, 300, "%s\n%s\n",_user_info.c_str(),_system_error.c_str()); 
    }



    const char * ScriptRepoException::what() const throw(){

      return general_info; 
    }
  } // namespace API
} // namespace Mantid

///\endcond TEMPLATE
