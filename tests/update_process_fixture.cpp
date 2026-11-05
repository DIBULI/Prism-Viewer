#include <filesystem>
#include <fstream>
#include <string>
int main(int argc,char** argv){
  const auto root=std::filesystem::absolute(argv[0]).parent_path();
  if(std::filesystem::exists(root/"fail-start"))return 9;
  if(argc==3&&std::string(argv[1])=="--viewer-update-ack")std::ofstream(argv[2])<<"started";
  else std::ofstream(root/"started-old")<<"started";
  return 0;
}
