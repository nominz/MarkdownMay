#include "markdownmay/export/docx_writer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
int RunDocxWriterTests(){using namespace markdownmay;using namespace markdownmay::exporting;document::DocumentSession s("# 标题\n\n正文有 **粗体**、[链接](https://example.com) 和 $a+b$。\n\n- 项目\n\n| 名称 | 状态 |\n|---|---|\n| 马冬梅 | 正常 |\n");auto x=s.snapshot();auto d=BuildExportDocument(x,x.source_revision,ExportScope::full,ExportFormat::docx);if(!d.is_ok())return 60;const bool keep=std::filesystem::exists("tmp/docx/.keep");auto dir=keep?std::filesystem::path("tmp/docx/fixture"):std::filesystem::temp_directory_path()/("mm-docx-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);auto p=dir/"sample.docx";if(ExportDocx(d.value(),p)!=ErrorCode::ok||ValidateDocx(p)!=ErrorCode::ok)return 61;std::ifstream f(p,std::ios::binary);std::string z{std::istreambuf_iterator<char>(f),{}};if(z.find("Heading1")==std::string::npos||z.find("w:hyperlink")==std::string::npos||z.find("w:tbl")==std::string::npos)return 62;if(!keep){std::error_code e;std::filesystem::remove_all(dir,e);}return 0;}
