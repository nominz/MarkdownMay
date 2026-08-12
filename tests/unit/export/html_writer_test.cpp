#include "markdownmay/export/html_writer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

int RunHtmlWriterTests(){using namespace markdownmay;using namespace markdownmay::exporting;
 document::DocumentSession s("# 标题\n\n正文 **粗体** <script>alert(1)</script> 和 $a<b$。\n\n```mermaid\ngraph TD; A-->B\n```\n\n[外链](https://example.invalid/)\n");auto x=s.snapshot();auto d=BuildExportDocument(x,x.source_revision,ExportScope::full,ExportFormat::html);if(!d.is_ok())return 70;
 const bool keep=std::filesystem::exists("tmp/html/.keep");auto dir=keep?std::filesystem::path("tmp/html/fixture"):std::filesystem::temp_directory_path()/("mm-html-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);auto p=dir/"sample.html";if(ExportHtml(d.value(),p)!=ErrorCode::ok||ValidateHtml(p)!=ErrorCode::ok)return 71;std::ifstream f(p,std::ios::binary);std::string h{std::istreambuf_iterator<char>(f),{}};
 if(h.find("default-src 'none'")==std::string::npos||h.find("connect-src 'none'")==std::string::npos||h.find("&lt;script&gt;")==std::string::npos||h.find("data-render-state=\"source-fallback\"")==std::string::npos||h.find("data-render-state=\"mathml-system-font\"")==std::string::npos)return 72;
 if(h.find("<script")!=std::string::npos||h.find("href=\"http")!=std::string::npos||h.find("src=\"http")!=std::string::npos)return 73;
 auto outline=BuildExportDocument(x,x.source_revision,ExportScope::outline,ExportFormat::txt);if(!outline.is_ok()||WriteHtml(outline.value(),dir/"bad.html",{}, {})!=ErrorCode::export_invalid_options)return 74;
 if(!keep){std::error_code e;std::filesystem::remove_all(dir,e);}return 0;}
