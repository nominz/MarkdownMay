#include "markdownmay/export/html_writer.hpp"

#include "markdownmay/fileio/text_encoding.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

namespace markdownmay::exporting { namespace {

std::string Escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break; case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break; default: out += c; }
    }
    return out;
}

std::string Base64(const std::vector<std::uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[i]) << 16 |
            (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0) |
            (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += alphabet[(value >> 18) & 63]; out += alphabet[(value >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return out;
}

std::string Mime(const std::vector<std::uint8_t>& b) {
    if (b.size() >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "image/png";
    if (b.size() >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return "image/jpeg";
    if (b.size() >= 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return "image/gif";
    if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M') return "image/bmp";
    return {};
}

const ExportResource* Resource(const ExportDocument& d, document::NodeId id) {
    const auto it = std::find_if(d.resources.begin(), d.resources.end(),
        [id](const ExportResource& resource) { return resource.source_id == id; });
    return it == d.resources.end() ? nullptr : &*it;
}

std::string Text(const ExportNode& n) {
    std::string out = n.text; for (const auto& child : n.children) out += Text(child); return out;
}

void Inline(const ExportDocument& d, const ExportNode& n, std::string& out) {
    if (n.kind == document::NodeKind::text) { out += Escape(n.text); return; }
    if (n.kind == document::NodeKind::inline_code) { out += "<code>" + Escape(n.text) + "</code>"; return; }
    if (n.kind == document::NodeKind::formula_inline || n.kind == document::NodeKind::formula_block) {
        out += "<span class=\"formula\" data-render-state=\"mathml-system-font\"><math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mtext>" + Escape(n.text) + "</mtext></math><code class=\"source\">" + Escape(n.text) + "</code></span>"; return;
    }
    if (n.kind == document::NodeKind::image) {
        const auto* r = Resource(d, n.source_id); const auto mime = r ? Mime(r->bytes) : std::string{};
        if (r && r->state == ExportResourceState::embedded && !mime.empty())
            out += "<img alt=\"" + Escape(Text(n)) + "\" src=\"data:" + mime + ";base64," + Base64(r->bytes) + "\">";
        else out += "<span class=\"blocked\">[图片不可用：" + Escape(Text(n)) + "]</span>";
        return;
    }
    std::string inner; for (const auto& child : n.children) Inline(d, child, inner);
    if (n.kind == document::NodeKind::strong) out += "<strong>" + inner + "</strong>";
    else if (n.kind == document::NodeKind::emphasis) out += "<em>" + inner + "</em>";
    else if (n.kind == document::NodeKind::strike) out += "<del>" + inner + "</del>";
    else if (n.kind == document::NodeKind::link) {
        const auto* a = std::get_if<document::LinkAttributes>(&n.attributes);
        out += "<span class=\"external-link\">" + inner + (a ? " (" + Escape(a->target) + ")" : "") + "</span>";
    } else out += inner;
}

std::string Inlines(const ExportDocument& d, const ExportNode& n) {
    std::string out; if (!n.text.empty()) out += Escape(n.text);
    for (const auto& child : n.children) Inline(d, child, out); return out;
}

void Block(const ExportDocument& d, const ExportNode& n, std::string& out, unsigned depth = 0) {
    switch (n.kind) {
    case document::NodeKind::heading: { unsigned level = 1; if (const auto* a = std::get_if<document::HeadingAttributes>(&n.attributes)) level = a->level; out += "<h" + std::to_string(level) + ">" + Inlines(d,n) + "</h" + std::to_string(level) + ">"; break; }
    case document::NodeKind::paragraph: out += "<p>" + Inlines(d,n) + "</p>"; break;
    case document::NodeKind::quote: out += "<blockquote>"; for (const auto& c:n.children) Block(d,c,out,depth); out += "</blockquote>"; break;
    case document::NodeKind::code_block: { const auto* a=std::get_if<document::CodeAttributes>(&n.attributes); if(a&&a->language=="mermaid") out += "<section class=\"mermaid\" data-render-state=\"source-fallback\"><strong>Mermaid 源码（未执行）</strong><pre><code>"+Escape(n.text)+"</code></pre></section>"; else out += "<pre><code>"+Escape(n.text)+"</code></pre>"; break; }
    case document::NodeKind::list: { const auto* a=std::get_if<document::ListAttributes>(&n.attributes); const auto tag=a&&a->ordered?"ol":"ul"; out += std::string("<")+tag+">"; for(const auto&i:n.children){out+="<li>";if(const auto*v=std::get_if<document::ListItemAttributes>(&i.attributes);v&&v->task)out+=v->checked?"☑ ":"☐ ";for(const auto&c:i.children){if(c.kind==document::NodeKind::list)Block(d,c,out,depth+1);else out+=Inlines(d,c);}out+="</li>";}out+=std::string("</")+tag+">";break;}
    case document::NodeKind::table: out+="<table>"; for(const auto&s:n.children)for(const auto&r:s.children){out+="<tr>";for(const auto&c:r.children)out+="<td>"+Inlines(d,c)+"</td>";out+="</tr>";}out+="</table>";break;
    case document::NodeKind::thematic_break: out += "<hr>"; break;
    case document::NodeKind::unknown_block: out += "<pre class=\"raw-html\"><code>"+Escape(n.text)+"</code></pre>"; break;
    default: { const auto text=Inlines(d,n); if(!text.empty())out+="<p>"+text+"</p>"; }
    }
}

}  // namespace

ErrorCode WriteHtml(const ExportDocument& d,const std::filesystem::path&p,const CancellationToken& c,const ExportProgressSink& progress) {
    if(c.is_cancelled()||d.scope!=ExportScope::full)return c.is_cancelled()?ErrorCode::export_cancelled:ErrorCode::export_invalid_options;
    std::string body; for(std::size_t i=0;i<d.blocks.size();++i){if(c.is_cancelled())return ErrorCode::export_cancelled;Block(d,d.blocks[i],body);if(progress)progress({ExportStage::writing,static_cast<std::uint32_t>(10+(i+1)*60/(std::max)(std::size_t{1},d.blocks.size())),100});}
    const std::string html="<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'none'; font-src 'none'; connect-src 'none'; object-src 'none'; media-src 'none'; frame-src 'none'; worker-src 'none'; manifest-src 'none'; base-uri 'none'; form-action 'none'\"><meta name=\"referrer\" content=\"no-referrer\"><meta name=\"color-scheme\" content=\"light dark\"><title>Markdown May 导出文档</title><style>:root{font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;line-height:1.65;color-scheme:light dark}body{max-width:52rem;margin:2rem auto;padding:0 1rem}img{max-width:100%;height:auto}pre,.source{white-space:pre-wrap;overflow-wrap:anywhere;background:#80808020;padding:.75rem;border-radius:.35rem}code{font-family:Consolas,monospace}table{border-collapse:collapse;width:100%}td{border:1px solid #888;padding:.4rem}blockquote{border-inline-start:.25rem solid #888;padding-inline-start:1rem}.blocked{color:#a33}.external-link{overflow-wrap:anywhere}math{font-family:'Cambria Math','Segoe UI Symbol',serif}.formula .source{margin-inline-start:.4rem}@media print{body{max-width:none;margin:0}}</style></head><body><main>"+body+"</main></body></html>";
    std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(html.data(),static_cast<std::streamsize>(html.size()));return f.good()?ErrorCode::ok:ErrorCode::export_target_failed;
}

ErrorCode ValidateHtml(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::string s{std::istreambuf_iterator<char>(f),{}};if(!fileio::IsValidUtf8(s)||!s.starts_with("<!doctype html>")||s.find("default-src 'none'")==std::string::npos||s.find("connect-src 'none'")==std::string::npos||s.find("script-src 'none'")==std::string::npos||s.find("</html>")==std::string::npos)return ErrorCode::export_validation_failed;for(const auto*bad:{"<script","javascript:","src=\"http","href=\"http","url(http","<iframe","<object","<form"})if(s.find(bad)!=std::string::npos)return ErrorCode::export_validation_failed;return ErrorCode::ok;}
ErrorCode ExportHtml(const ExportDocument&d,const std::filesystem::path&p,const CancellationToken&c,const ExportProgressSink&s){return RunExportTask(d,p,WriteHtml,ValidateHtml,c,s);}
}  // namespace markdownmay::exporting
