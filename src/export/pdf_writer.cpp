#include "markdownmay/export/pdf_writer.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fontsub.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <cstdlib>

namespace markdownmay::exporting {
namespace {
using Microsoft::WRL::ComPtr;
struct Font { std::vector<unsigned char> bytes; std::map<wchar_t, WORD> glyphs; std::map<WORD,unsigned> widths; };
struct Text { std::wstring value; double x{}, y{}, size{}; };
struct Link { double x1{}, y1{}, x2{}, y2{}; std::string target; };
struct Image { std::vector<unsigned char> rgb; UINT width{}, height{}; double x{}, y{}, w{}, h{}; };
struct Page { std::vector<Text> text; std::vector<Link> links; std::vector<Image> images; };

std::wstring Wide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string Visible(const ExportNode& node) {
    if (node.kind == document::NodeKind::formula_inline) return "[公式：" + node.text + "]";
    if (node.kind == document::NodeKind::formula_block) return "[块公式：" + node.text + "]";
    std::string value = node.text;
    for (const auto& child : node.children) value += Visible(child);
    return value;
}
std::string Literal(std::string_view value) {
    std::string out;
    for (const unsigned char byte : value) {
        if (byte == '(' || byte == ')' || byte == '\\') out.push_back('\\');
        if (byte >= 32U) out.push_back(static_cast<char>(byte));
    }
    return out;
}
bool LoadFont(Font& font) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT handle = CreateFontW(-1000, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"SimHei");
    const auto old = SelectObject(dc, handle);
    const DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
    bool ok = size != GDI_ERROR;
    if (ok) { font.bytes.resize(size); ok = GetFontData(dc, 0, 0, font.bytes.data(), size) != GDI_ERROR; }
    SelectObject(dc, old); DeleteObject(handle); DeleteDC(dc);
    return ok;
}
std::string Glyphs(std::wstring_view value, Font& font) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT handle = CreateFontW(-1000, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"SimHei");
    const auto old = SelectObject(dc, handle);
    std::vector<WORD> ids(value.size());
    GetGlyphIndicesW(dc, value.data(), static_cast<int>(value.size()), ids.data(), GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc, old); DeleteObject(handle); DeleteDC(dc);
    std::ostringstream out; out << '<' << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == 0xffffU) ids[i] = 0;
        font.glyphs.emplace(value[i], ids[i]); font.widths.emplace(ids[i],value[i]<128?500U:1000U); out << std::setw(4) << unsigned{ids[i]};
    }
    return out.str() + ">";
}
bool SubsetFont(Font& font) {
    std::vector<unsigned short> keep{0};
    for (const auto& entry : font.glyphs) keep.push_back(entry.second);
    std::sort(keep.begin(), keep.end()); keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    unsigned char* output{}; unsigned long capacity{}, written{};
    const auto result = CreateFontPackage(font.bytes.data(), static_cast<unsigned long>(font.bytes.size()),
        &output, &capacity, &written, TTFCFP_FLAGS_SUBSET | TTFCFP_FLAGS_GLYPHLIST,
        0, TTFCFP_SUBSET, TTFCFP_LANG_KEEP_ALL, TTFCFP_MS_PLATFORMID,
        TTFCFP_UNICODE_CHAR_SET, keep.data(), static_cast<unsigned short>(keep.size()),
        std::malloc, std::realloc, std::free, nullptr);
    if (result != NO_ERROR || !output || written == 0) { if (output) std::free(output); return false; }
    font.bytes.assign(output, output + written); std::free(output); return true;
}
bool DecodeImage(const ExportResource& resource, Image& image) {
    if (resource.state != ExportResourceState::embedded || resource.bytes.empty()) return false;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(
        const_cast<BYTE*>(resource.bytes.data()), static_cast<DWORD>(resource.bytes.size())))) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&image.width, &image.height)) ||
        image.width == 0 || image.height == 0 || static_cast<std::uint64_t>(image.width) * image.height > 100000000ULL) return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(frame.Get(),
        GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    const UINT stride = image.width * 3U;
    image.rgb.resize(static_cast<std::size_t>(stride) * image.height);
    return SUCCEEDED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(image.rgb.size()), image.rgb.data()));
}

class Layout {
public:
    Layout(double w, double h, double margin, const ExportDocument& doc)
        : w_(w), h_(h), margin_(margin), y_(h-margin), document_(doc) { pages_.emplace_back(); }
    void line(std::string_view utf8, double size=11, double indent=0, std::string_view target={}) {
        auto text = Wide(utf8); const double cw = size * .55;
        const std::size_t columns = static_cast<std::size_t>((std::max)(1.0,
            (w_-margin_*2-indent)/cw));
        for (std::size_t at=0; at<text.size() || (at==0 && text.empty());) {
            std::size_t count=(std::min)(columns,text.size()-at); auto nl=text.find(L'\n',at);
            if (nl!=std::wstring::npos && nl<at+count) count=nl-at;
            ensure(size*1.45); auto part=text.substr(at,count);
            if (!part.empty() && part.back()==L'\r') part.pop_back();
            pages_.back().text.push_back({part,margin_+indent,y_,size});
            if (!target.empty()) pages_.back().links.push_back({margin_+indent,y_-2,
                margin_+indent+part.size()*cw,y_+size,std::string(target)});
            y_-=size*1.45; at+=count; if(at<text.size()&&text[at]==L'\n')++at;
            if(count==0&&at<text.size())++at; if(text.empty()) break;
        }
    }
    void gap(double n=5){y_-=n;}
    void picture(document::NodeId id, std::string_view alt) {
        const auto it=std::find_if(document_.resources.begin(),document_.resources.end(),
            [id](const ExportResource& r){return r.source_id==id;});
        Image image;
        if(it==document_.resources.end() || !DecodeImage(*it,image)) { line("[图片不可用："+std::string(alt)+"]",10); return; }
        double width=(std::min)(w_-margin_*2,static_cast<double>(image.width)*.75);
        double height=width*image.height/image.width;
        if(height>h_-margin_*2){height=h_-margin_*2;width=height*image.width/image.height;}
        ensure(height+6); image.x=margin_; image.y=y_-height; image.w=width; image.h=height;
        pages_.back().images.push_back(std::move(image)); y_-=height+6;
    }
    void node(const ExportNode& n,unsigned depth=0) {
        switch(n.kind) {
        case document::NodeKind::heading:{unsigned level=1;if(auto*a=std::get_if<document::HeadingAttributes>(&n.attributes))level=a->level;line(Visible(n),(std::max)(12.0,23.0-level*2.0),(level-1)*12.0);gap();break;}
        case document::NodeKind::paragraph:{std::string target;for(const auto&c:n.children){if(c.kind==document::NodeKind::image){picture(c.source_id,Visible(c));continue;}if(c.kind==document::NodeKind::link)if(auto*a=std::get_if<document::LinkAttributes>(&c.attributes))target=a->target;}line(Visible(n),11,depth*14.0,target);gap();break;}
        case document::NodeKind::quote:for(const auto&c:n.children)node(c,depth+1);break;
        case document::NodeKind::list:{auto*a=std::get_if<document::ListAttributes>(&n.attributes);unsigned no=a?a->start:1;for(const auto&i:n.children){std::string s=a&&a->ordered?std::to_string(no++)+". ":"- ";if(auto*li=std::get_if<document::ListItemAttributes>(&i.attributes);li&&li->task)s+=li->checked?"[x] ":"[ ] ";for(const auto&c:i.children)if(c.kind!=document::NodeKind::list)s+=Visible(c);line(s,11,depth*14.0);for(const auto&c:i.children)if(c.kind==document::NodeKind::list)node(c,depth+1);}gap();break;}
        case document::NodeKind::code_block:{auto*a=std::get_if<document::CodeAttributes>(&n.attributes);if(a&&a->language=="mermaid")line("[Mermaid 源码]",10);line(n.text,9,10);gap();break;}
        case document::NodeKind::table:table(n);break;
        case document::NodeKind::thematic_break:line("----------------------------------------",9);break;
        case document::NodeKind::unknown_block:line("[原始 HTML]",10);line(n.text,9);break;
        default:{auto value=Visible(n);if(!value.empty())line(value);break;}}
    }
    std::vector<Page> take(){return std::move(pages_);}
private:
    void ensure(double need){if(y_-need<margin_){pages_.emplace_back();y_=h_-margin_;}}
    void table(const ExportNode& table){std::vector<std::string> header;for(const auto&s:table.children)for(const auto&r:s.children){std::vector<std::string> cells;for(const auto&c:r.children)cells.push_back(Visible(c));if(header.empty())header=cells;if(y_-16<margin_){pages_.emplace_back();y_=h_-margin_;row(header);}row(cells);}gap();}
    void row(const std::vector<std::string>& cells){std::string value;for(const auto&c:cells){if(!value.empty())value+="    |    ";value+=c;}line(value,10);}
    double w_,h_,margin_,y_;const ExportDocument& document_;std::vector<Page> pages_;
};

std::string Stream(std::string_view data,std::string_view extra={}){return "<< /Length "+std::to_string(data.size())+std::string(extra)+" >>\nstream\n"+std::string(data)+"\nendstream";}
bool WriteFile(const std::filesystem::path& path,const std::vector<std::string>& objects){std::string pdf="%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";std::vector<std::size_t> off(objects.size()+1);for(std::size_t i=0;i<objects.size();++i){off[i+1]=pdf.size();pdf+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}auto x=pdf.size();std::ostringstream t;t<<"xref\n0 "<<off.size()<<"\n0000000000 65535 f \n";for(std::size_t i=1;i<off.size();++i)t<<std::setw(10)<<std::setfill('0')<<off[i]<<" 00000 n \n";t<<"trailer\n<< /Size "<<off.size()<<" /Root 1 0 R >>\nstartxref\n"<<x<<"\n%%EOF\n";pdf+=t.str();std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(pdf.data(),static_cast<std::streamsize>(pdf.size()));return out.good();}
} // namespace

ErrorCode WritePdf(const ExportDocument& document,const std::filesystem::path& path,const CancellationToken& cancel,const ExportProgressSink& progress,const PdfOptions& options){
    if(cancel.is_cancelled())return ErrorCode::export_cancelled;if(options.margin_points<18||options.margin_points>144)return ErrorCode::export_invalid_options;
    const double w=options.landscape?841.89:595.28,h=options.landscape?595.28:841.89;Layout layout(w,h,options.margin_points,document);
    for(std::size_t i=0;i<document.blocks.size();++i){if(cancel.is_cancelled())return ErrorCode::export_cancelled;layout.node(document.blocks[i]);if(progress)progress({ExportStage::writing,static_cast<std::uint32_t>(10+(i+1)*40/(std::max)(std::size_t{1},document.blocks.size())),100});}
    auto pages=layout.take();Font font;if(!LoadFont(font))return ErrorCode::export_target_failed;std::vector<std::string> content;
    for(const auto&p:pages){std::ostringstream s;for(const auto&r:p.text)s<<"BT /F1 "<<r.size<<" Tf 1 0 0 1 "<<r.x<<' '<<r.y<<" Tm "<<Glyphs(r.value,font)<<" Tj ET\n";for(std::size_t n=0;n<p.images.size();++n){const auto&i=p.images[n];s<<"q "<<i.w<<" 0 0 "<<i.h<<' '<<i.x<<' '<<i.y<<" cm /Im"<<n+1<<" Do Q\n";}content.push_back(s.str());}
    std::ostringstream cmap;cmap<<"/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /MM def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";for(auto it=font.glyphs.begin();it!=font.glyphs.end();){auto count=(std::min)(std::size_t{100},static_cast<std::size_t>(std::distance(it,font.glyphs.end())));cmap<<count<<" beginbfchar\n";for(std::size_t n=0;n<count;++n,++it)cmap<<'<'<<std::hex<<std::setw(4)<<std::setfill('0')<<it->second<<"> <"<<std::setw(4)<<unsigned{it->first}<<">\n";cmap<<"endbfchar\n";}cmap<<"endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend";
    if(!SubsetFont(font))return ErrorCode::export_target_failed;std::ostringstream widths;for(const auto&[id,width]:font.widths)widths<<id<<" ["<<width<<"] ";
    std::vector<std::string> obj{"<< /Type /Catalog /Pages 2 0 R >>","","<< /Type /Font /Subtype /Type0 /BaseFont /SimHei /Encoding /Identity-H /DescendantFonts [4 0 R] /ToUnicode 7 0 R >>","<< /Type /Font /Subtype /CIDFontType2 /BaseFont /SimHei /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor 5 0 R /CIDToGIDMap /Identity /DW 1000 /W ["+widths.str()+"] >>","<< /Type /FontDescriptor /FontName /SimHei /Flags 4 /FontBBox [-170 -300 1100 1050] /ItalicAngle 0 /Ascent 900 /Descent -220 /CapHeight 700 /StemV 80 /FontFile2 6 0 R >>",Stream(std::string(reinterpret_cast<const char*>(font.bytes.data()),font.bytes.size())," /Length1 "+std::to_string(font.bytes.size())),Stream(cmap.str())};
    std::ostringstream kids;for(std::size_t i=0;i<pages.size();++i)kids<<8+i*2<<" 0 R ";obj[1]="<< /Type /Pages /Count "+std::to_string(pages.size())+" /Kids ["+kids.str()+"] >>";
    std::size_t image_id=8+pages.size()*2;for(std::size_t i=0;i<pages.size();++i){std::ostringstream ann;if(!pages[i].links.empty()){ann<<" /Annots [";for(const auto&l:pages[i].links)ann<<"<< /Type /Annot /Subtype /Link /Rect ["<<l.x1<<' '<<l.y1<<' '<<l.x2<<' '<<l.y2<<"] /Border [0 0 0] /A << /S /URI /URI ("<<Literal(l.target)<<") >> >> ";ann<<']';}std::ostringstream xo;if(!pages[i].images.empty()){xo<<" /XObject << ";for(std::size_t n=0;n<pages[i].images.size();++n)xo<<"/Im"<<n+1<<' '<<image_id+n<<" 0 R ";xo<<">>";}obj.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+std::to_string(w)+" "+std::to_string(h)+"] /Resources << /Font << /F1 3 0 R >>"+xo.str()+" >> /Contents "+std::to_string(9+i*2)+" 0 R"+ann.str()+" >>");obj.push_back(Stream(content[i]));image_id+=pages[i].images.size();}
    for(const auto&p:pages)for(const auto&i:p.images)obj.push_back(Stream(std::string(reinterpret_cast<const char*>(i.rgb.data()),i.rgb.size())," /Type /XObject /Subtype /Image /Width "+std::to_string(i.width)+" /Height "+std::to_string(i.height)+" /ColorSpace /DeviceRGB /BitsPerComponent 8"));
    if(cancel.is_cancelled())return ErrorCode::export_cancelled;return WriteFile(path,obj)?ErrorCode::ok:ErrorCode::export_target_failed;
}
ErrorCode ValidatePdf(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)return ErrorCode::export_validation_failed;std::string p{std::istreambuf_iterator<char>(in),{}};return p.starts_with("%PDF-")&&p.ends_with("%%EOF\n")&&p.find("/Type /Catalog")!=std::string::npos&&p.find("/ToUnicode")!=std::string::npos&&p.find("xref\n")!=std::string::npos?ErrorCode::ok:ErrorCode::export_validation_failed;}
ErrorCode ExportPdf(const ExportDocument& document,const std::filesystem::path& target,const CancellationToken& cancel,const ExportProgressSink& progress,const PdfOptions& options){ExportWriter writer=[&options](const ExportDocument&d,const std::filesystem::path&p,const CancellationToken&c,const ExportProgressSink&s){return WritePdf(d,p,c,s,options);};return RunExportTask(document,target,writer,ValidatePdf,cancel,progress);}
} // namespace markdownmay::exporting
