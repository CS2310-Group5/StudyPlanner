#include "logging.h"
#include <filesystem>
#include <fstream>
#include <ctime>

std::string logPath(){return ".cityu_logs/errors.log";}
void writeErrorLog(const std::string &code,const std::string &plain,
                   const std::string &action,const std::string &technical){
    std::filesystem::create_directories(".cityu_logs");
    std::ofstream f(logPath(),std::ios::app);
    std::time_t t=std::time(nullptr);char buf[64]={};std::strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",std::localtime(&t));
    f<<"["<<buf<<"] "<<code<<" - "<<plain<<"\n";
    f<<"Suggested action: "<<action<<"\n";
    f<<"Technical details: "<<technical<<"\n\n";
}
