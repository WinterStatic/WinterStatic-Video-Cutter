#include "subtitle_sync_core.h"

#include "pgs_sync.h"
#include "subtitle_encoding.h"
#include "vobsub_sync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace winterstatic::subsync {
namespace {

using winterstatic::subtitle::normalize_text_to_utf8;

std::wstring lower_ext(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return ext;
}

bool read_binary(const std::filesystem::path& path, std::string& bytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), {});
    return static_cast<bool>(f) || f.eof();
}

bool write_binary(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::string trim_ascii(std::string value) {
    auto is_ws = [](char ch) { return ch == ' ' || ch == '\t'; };
    while (!value.empty() && is_ws(value.front())) value.erase(value.begin());
    while (!value.empty() && is_ws(value.back())) value.pop_back();
    return value;
}

bool parse_integer(std::string_view s, long long& out) {
    std::string t(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    if (!end || end == t.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_srt_clock(std::string_view raw, std::int64_t& ms) {
    // HH:MM:SS,mmm
    std::string text = trim_ascii(std::string(raw));
    const auto c1 = text.find(':');
    const auto c2 = c1 == std::string::npos ? std::string::npos : text.find(':', c1 + 1);
    const auto comma = c2 == std::string::npos ? std::string::npos : text.find(',', c2 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos || comma == std::string::npos)
        return false;

    long long h=0,m=0,s=0,frac=0;
    if (!parse_integer(text.substr(0,c1),h) ||
        !parse_integer(text.substr(c1+1,c2-c1-1),m) ||
        !parse_integer(text.substr(c2+1,comma-c2-1),s))
        return false;
    std::string f = text.substr(comma+1);
    if (f.empty() || f.size() > 3) return false;
    for (char ch : f) if (ch < '0' || ch > '9') return false;
    while (f.size() < 3) f.push_back('0');
    if (!parse_integer(f, frac)) return false;
    if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59) return false;
    ms = ((h*60+m)*60+s)*1000 + frac;
    return true;
}

bool parse_vtt_clock(std::string_view raw, std::int64_t& ms) {
    // Accept MM:SS.mmm and HH:MM:SS.mmm.
    std::string text = trim_ascii(std::string(raw));
    const auto dot = text.rfind('.');
    if (dot == std::string::npos) return false;
    std::string fraction = text.substr(dot+1);
    if (fraction.size() != 3) return false;
    for (char ch : fraction) if (ch < '0' || ch > '9') return false;

    std::vector<std::string> parts;
    std::string left = text.substr(0,dot);
    std::size_t start=0;
    while (true) {
        const auto pos = left.find(':', start);
        parts.push_back(left.substr(start, pos == std::string::npos ? std::string::npos : pos-start));
        if (pos == std::string::npos) break;
        start = pos+1;
    }
    if (parts.size() != 2 && parts.size() != 3) return false;

    long long h=0,m=0,s=0,frac=0;
    if (parts.size() == 2) {
        if (!parse_integer(parts[0],m) || !parse_integer(parts[1],s)) return false;
    } else {
        if (!parse_integer(parts[0],h) || !parse_integer(parts[1],m) || !parse_integer(parts[2],s))
            return false;
    }
    if (!parse_integer(fraction, frac)) return false;
    if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59) return false;
    ms = ((h*60+m)*60+s)*1000 + frac;
    return true;
}

bool parse_ass_clock(std::string_view raw, std::int64_t& ms) {
    // H:MM:SS.cc
    std::string text = trim_ascii(std::string(raw));
    const auto c1=text.find(':');
    const auto c2=c1==std::string::npos?std::string::npos:text.find(':',c1+1);
    const auto dot=c2==std::string::npos?std::string::npos:text.find('.',c2+1);
    if (c1==std::string::npos || c2==std::string::npos || dot==std::string::npos) return false;
    long long h=0,m=0,s=0,frac=0;
    if (!parse_integer(text.substr(0,c1),h) ||
        !parse_integer(text.substr(c1+1,c2-c1-1),m) ||
        !parse_integer(text.substr(c2+1,dot-c2-1),s))
        return false;
    std::string f=text.substr(dot+1);
    if (f.empty() || f.size()>3) return false;
    for(char ch:f) if(ch<'0'||ch>'9') return false;
    while(f.size()<3) f.push_back('0');
    if(!parse_integer(f,frac)) return false;
    if(h<0||m<0||m>59||s<0||s>59) return false;
    ms=((h*60+m)*60+s)*1000+frac;
    return true;
}

std::string format_srt(std::int64_t ms) {
    ms=std::max<std::int64_t>(0,ms);
    char b[64]{};
    std::snprintf(b,sizeof(b),"%02lld:%02lld:%02lld,%03lld",
        static_cast<long long>(ms/3600000),
        static_cast<long long>((ms/60000)%60),
        static_cast<long long>((ms/1000)%60),
        static_cast<long long>(ms%1000));
    return b;
}

std::string format_vtt(std::int64_t ms, bool had_hours) {
    ms=std::max<std::int64_t>(0,ms);
    char b[64]{};
    if (had_hours || ms >= 3600000) {
        std::snprintf(b,sizeof(b),"%02lld:%02lld:%02lld.%03lld",
            static_cast<long long>(ms/3600000),
            static_cast<long long>((ms/60000)%60),
            static_cast<long long>((ms/1000)%60),
            static_cast<long long>(ms%1000));
    } else {
        std::snprintf(b,sizeof(b),"%02lld:%02lld.%03lld",
            static_cast<long long>(ms/60000),
            static_cast<long long>((ms/1000)%60),
            static_cast<long long>(ms%1000));
    }
    return b;
}

std::string format_ass(std::int64_t ms) {
    ms=std::max<std::int64_t>(0,ms);
    ms=((ms+5)/10)*10;
    char b[64]{};
    std::snprintf(b,sizeof(b),"%lld:%02lld:%02lld.%02lld",
        static_cast<long long>(ms/3600000),
        static_cast<long long>((ms/60000)%60),
        static_cast<long long>((ms/1000)%60),
        static_cast<long long>((ms%1000)/10));
    return b;
}

bool starts_with_nocase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t i=0;i<prefix.size();++i) {
        char a=text[i], b=prefix[i];
        if(a>='A'&&a<='Z') a=static_cast<char>(a-'A'+'a');
        if(b>='A'&&b<='Z') b=static_cast<char>(b-'A'+'a');
        if(a!=b) return false;
    }
    return true;
}

struct TextRewriteStats {
    std::size_t changed = 0;
    std::size_t clamped = 0;
};

std::int64_t transform_counted(std::int64_t old_ms, const SyncSpec& spec, TextRewriteStats& stats) {
    const auto out = transform_ms(old_ms, spec);
    if (out != old_ms) ++stats.changed;
    if (out == 0 && old_ms != 0) ++stats.clamped;
    return out;
}

bool rewrite_arrow_line(std::string& line, bool vtt, const SyncSpec& spec, TextRewriteStats& stats) {
    const auto arrow=line.find("-->");
    if(arrow==std::string::npos) return true;

    std::size_t sb=0;
    while(sb<arrow && (line[sb]==' '||line[sb]=='\t')) ++sb;
    std::size_t se=arrow;
    while(se>sb && (line[se-1]==' '||line[se-1]=='\t')) --se;

    std::size_t eb=arrow+3;
    while(eb<line.size() && (line[eb]==' '||line[eb]=='\t')) ++eb;
    std::size_t ee=eb;
    while(ee<line.size() && line[ee]!=' ' && line[ee]!='\t' && line[ee]!='\r') ++ee;

    std::int64_t a=0,b=0;
    const bool ok1=vtt?parse_vtt_clock(line.substr(sb,se-sb),a):parse_srt_clock(line.substr(sb,se-sb),a);
    const bool ok2=vtt?parse_vtt_clock(line.substr(eb,ee-eb),b):parse_srt_clock(line.substr(eb,ee-eb),b);
    if(!ok1 && !ok2) return true;
    if(!ok1 || !ok2) return false;

    const bool start_had_hours = std::count(line.begin()+static_cast<std::ptrdiff_t>(sb),
                                            line.begin()+static_cast<std::ptrdiff_t>(se), ':') >= 2;
    const bool end_had_hours = std::count(line.begin()+static_cast<std::ptrdiff_t>(eb),
                                          line.begin()+static_cast<std::ptrdiff_t>(ee), ':') >= 2;
    const auto na=transform_counted(a,spec,stats);
    const auto nb=transform_counted(b,spec,stats);
    const std::string sa=vtt?format_vtt(na,start_had_hours):format_srt(na);
    const std::string sbv=vtt?format_vtt(nb,end_had_hours):format_srt(nb);

    line.replace(eb,ee-eb,sbv);
    line.replace(sb,se-sb,sa);
    return true;
}

bool rewrite_ass_line(std::string& line, const SyncSpec& spec, TextRewriteStats& stats) {
    if(!starts_with_nocase(line,"Dialogue:")) return true;
    const auto p=line.find(':');
    const auto c1=p==std::string::npos?std::string::npos:line.find(',',p+1);
    const auto c2=c1==std::string::npos?std::string::npos:line.find(',',c1+1);
    const auto c3=c2==std::string::npos?std::string::npos:line.find(',',c2+1);
    if(c1==std::string::npos||c2==std::string::npos||c3==std::string::npos) return false;
    std::int64_t a=0,b=0;
    if(!parse_ass_clock(line.substr(c1+1,c2-c1-1),a) ||
       !parse_ass_clock(line.substr(c2+1,c3-c2-1),b)) return false;
    const auto na=transform_counted(a,spec,stats);
    const auto nb=transform_counted(b,spec,stats);
    line.replace(c2+1,c3-c2-1,format_ass(nb));
    line.replace(c1+1,c2-c1-1,format_ass(na));
    return true;
}

std::vector<std::pair<std::size_t,std::size_t>> nonblank_blocks(const std::vector<std::string>& lines) {
    std::vector<std::pair<std::size_t,std::size_t>> out;
    std::size_t i=0;
    while(i<lines.size()) {
        while(i<lines.size() && trim_ascii(lines[i]).empty()) ++i;
        if(i>=lines.size()) break;
        const std::size_t begin=i;
        while(i<lines.size() && !trim_ascii(lines[i]).empty()) ++i;
        out.push_back({begin,i}); // [begin,end)
    }
    return out;
}

bool looks_like_vtt_metadata_block(std::string_view first) {
    std::string t=trim_ascii(std::string(first));
    return starts_with_nocase(t,"WEBVTT") ||
           starts_with_nocase(t,"NOTE") ||
           starts_with_nocase(t,"STYLE") ||
           starts_with_nocase(t,"REGION");
}

bool rewrite_vtt_structure_safe(std::string& data, const SyncSpec& spec, TextRewriteStats& stats) {
    // Preserve exact line endings by storing each line's EOL separately.
    struct L { std::string text; std::string eol; };
    std::vector<L> pieces;
    std::size_t pos=0;
    while(pos<data.size()) {
        const auto nl=data.find('\n',pos);
        if(nl==std::string::npos) {
            pieces.push_back({data.substr(pos),""});
            break;
        }
        std::size_t text_end=nl;
        std::string eol="\n";
        if(text_end>pos && data[text_end-1]=='\r') {
            --text_end;
            eol="\r\n";
        }
        pieces.push_back({data.substr(pos,text_end-pos),eol});
        pos=nl+1;
    }
    if(data.empty()) pieces.push_back({"",""});

    std::vector<std::string> just;
    just.reserve(pieces.size());
    for(auto& p:pieces) just.push_back(p.text);

    for(auto [begin,end]:nonblank_blocks(just)) {
        if(begin>=end || looks_like_vtt_metadata_block(just[begin])) continue;
        // Cue timing is first line, or second line when first is cue ID.
        for(std::size_t idx=begin; idx<std::min(end,begin+2); ++idx) {
            if(just[idx].find("-->")==std::string::npos) continue;
            std::string line=just[idx];
            if(!rewrite_arrow_line(line,true,spec,stats)) return false;
            if(line!=just[idx]) pieces[idx].text=std::move(line);
            break;
        }
    }

    std::string out;
    out.reserve(data.size()+64);
    for(auto& p:pieces) { out+=p.text; out+=p.eol; }
    data.swap(out);
    return true;
}

SyncResult apply_text(const std::filesystem::path& input, SubtitleKind kind,
                      const SyncSpec& spec, bool overwrite) {
    SyncResult result;
    result.kind=kind;

    std::string raw;
    if(!read_binary(input,raw)) {
        result.message="Could not read subtitle file.";
        return result;
    }

    bool had_utf8_bom=false;
    if(raw.size()>=3 &&
       static_cast<unsigned char>(raw[0])==0xEF &&
       static_cast<unsigned char>(raw[1])==0xBB &&
       static_cast<unsigned char>(raw[2])==0xBF) {
        had_utf8_bom=true;
    }

    const auto norm=normalize_text_to_utf8(raw);
    if(!norm.success) {
        result.message=norm.message;
        return result;
    }
    std::string data=norm.utf8;

    TextRewriteStats stats{};
    bool ok=true;

    if(kind==SubtitleKind::WebVtt) {
        ok=rewrite_vtt_structure_safe(data,spec,stats);
    } else {
        std::string out;
        out.reserve(data.size()+64);
        std::size_t pos=0;
        while(pos<data.size()) {
            const auto nl=data.find('\n',pos);
            std::string line=data.substr(pos,nl==std::string::npos?std::string::npos:nl-pos);
            const bool cr=!line.empty()&&line.back()=='\r';
            if(cr) line.pop_back();

            bool line_ok=true;
            if(kind==SubtitleKind::Srt)
                line_ok=rewrite_arrow_line(line,false,spec,stats);
            else if(kind==SubtitleKind::Ass||kind==SubtitleKind::Ssa)
                line_ok=rewrite_ass_line(line,spec,stats);
            else
                line_ok=false;

            if(!line_ok) { ok=false; break; }
            out+=line;
            if(cr) out+='\r';
            if(nl!=std::string::npos) { out+='\n'; pos=nl+1; }
            else break;
        }
        if(ok) data.swap(out);
    }

    if(!ok) {
        result.message="Could not parse one of the subtitle timing lines.";
        return result;
    }
    if(stats.changed==0) {
        // Identity/no-op is still a valid operation. Useful for testing and
        // integration because it proves the parser can round-trip the file.
    }

    // Preserve UTF-8 BOM when the original used one. UTF-16 inputs are
    // deliberately normalized to UTF-8 because the tested timing modules work
    // on UTF-8 text; this is reported to the caller.
    if(had_utf8_bom) data.insert(0,"\xEF\xBB\xBF",3);

    const auto output=default_output_path(input);
    if(std::filesystem::exists(output) && !overwrite) {
        result.message=std::string("Output already exists: ")+output.string();
        return result;
    }
    if(!write_binary(output,data)) {
        result.message="Could not write synced subtitle file.";
        return result;
    }

    result.success=true;
    result.output_primary=output;
    result.timestamps_changed=stats.changed;
    result.timestamps_clamped=stats.clamped;
    result.message=norm.converted && !had_utf8_bom
        ? "Saved synced subtitle (input normalized to UTF-8)."
        : "Saved synced subtitle.";
    return result;
}

std::filesystem::path paired_vob(const std::filesystem::path& input, std::wstring ext) {
    auto p=input;
    p.replace_extension(ext);
    return p;
}

} // namespace

SubtitleKind detect_kind(const std::filesystem::path& input) {
    const auto ext=lower_ext(input);
    if(ext==L".srt") return SubtitleKind::Srt;
    if(ext==L".ass") return SubtitleKind::Ass;
    if(ext==L".ssa") return SubtitleKind::Ssa;
    if(ext==L".vtt") return SubtitleKind::WebVtt;
    if(ext==L".sup") return SubtitleKind::Pgs;
    if(ext==L".idx") {
        if(std::filesystem::exists(paired_vob(input,L".sub"))) return SubtitleKind::VobSub;
        return SubtitleKind::Unknown;
    }
    if(ext==L".sub") {
        if(std::filesystem::exists(paired_vob(input,L".idx"))) return SubtitleKind::VobSub;
        return SubtitleKind::Unknown;
    }
    return SubtitleKind::Unknown;
}

const char* kind_name(SubtitleKind kind) {
    switch(kind) {
        case SubtitleKind::Srt: return "SRT / SubRip";
        case SubtitleKind::Ass: return "ASS";
        case SubtitleKind::Ssa: return "SSA";
        case SubtitleKind::WebVtt: return "WebVTT";
        case SubtitleKind::Pgs: return "PGS / SUP";
        case SubtitleKind::VobSub: return "VobSub / IDX+SUB";
        default: return "Unsupported";
    }
}

bool drift_geometry_valid(const SyncSpec& spec) {
    double v1=spec.video_a_seconds;
    double v2=spec.video_b_seconds;
    double o1=static_cast<double>(spec.offset_a_ms)/1000.0;
    double o2=static_cast<double>(spec.offset_b_ms)/1000.0;
    if(v2<v1) { std::swap(v1,v2); std::swap(o1,o2); }
    const double video_span=v2-v1;
    const double source1=v1-o1;
    const double source2=v2-o2;
    const double source_span=source2-source1;
    return video_span>0.050 && source_span>0.050;
}

std::int64_t transform_ms(std::int64_t old_ms, const SyncSpec& spec) {
    double corrected=static_cast<double>(old_ms);

    if(spec.drift_enabled && drift_geometry_valid(spec)) {
        double v1=spec.video_a_seconds*1000.0;
        double v2=spec.video_b_seconds*1000.0;
        double o1=static_cast<double>(spec.offset_a_ms);
        double o2=static_cast<double>(spec.offset_b_ms);
        if(v2<v1) { std::swap(v1,v2); std::swap(o1,o2); }

        const double s1=v1-o1;
        const double s2=v2-o2;
        const double corrected_span=v2-v1;
        const double source_span=s2-s1;

        if(corrected_span<=1.0 || source_span<=1.0) {
            corrected += static_cast<double>(spec.constant_offset_ms);
        } else if(old_ms<=s1) {
            corrected += o1;
        } else if(old_ms>=s2) {
            corrected += o2;
        } else {
            corrected=v1+(static_cast<double>(old_ms)-s1)*(corrected_span/source_span);
        }
    } else {
        corrected += static_cast<double>(spec.constant_offset_ms);
    }

    return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::llround(corrected)));
}

std::filesystem::path default_output_path(const std::filesystem::path& input) {
    const auto kind=detect_kind(input);
    if(kind==SubtitleKind::VobSub) {
        std::filesystem::path primary=input;
        if(lower_ext(primary)==L".sub") primary.replace_extension(L".idx");
        const auto parent=primary.parent_path();
        const auto stem=primary.stem().wstring()+L".synced";
        return parent/(stem+L".idx");
    }
    const auto parent=input.parent_path();
    const auto stem=input.stem().wstring()+L".synced";
    return parent/(stem+input.extension().wstring());
}

SyncResult apply_sync(const std::filesystem::path& input, const SyncSpec& spec, bool overwrite) {
    const auto kind=detect_kind(input);

    if(kind==SubtitleKind::Srt || kind==SubtitleKind::Ass ||
       kind==SubtitleKind::Ssa || kind==SubtitleKind::WebVtt) {
        return apply_text(input,kind,spec,overwrite);
    }

    if(kind==SubtitleKind::Pgs) {
        SyncResult r;
        r.kind=kind;
        winterstatic::subtitle::PgsSyncSpec ps{};
        ps.constant_offset_ms=static_cast<int>(std::clamp<std::int64_t>(spec.constant_offset_ms,-2147483647LL,2147483647LL));
        ps.drift_enabled=spec.drift_enabled && drift_geometry_valid(spec);
        ps.video_a_seconds=spec.video_a_seconds;
        ps.video_b_seconds=spec.video_b_seconds;
        ps.offset_a_ms=static_cast<int>(std::clamp<std::int64_t>(spec.offset_a_ms,-2147483647LL,2147483647LL));
        ps.offset_b_ms=static_cast<int>(std::clamp<std::int64_t>(spec.offset_b_ms,-2147483647LL,2147483647LL));

        const auto out=default_output_path(input);
        const auto rr=winterstatic::subtitle::rewrite_pgs_sup_timing(input,out,ps,overwrite);
        r.success=rr.success;
        r.message=rr.message;
        r.output_primary=out;
        r.timestamps_changed=rr.pts_changed+rr.dts_changed;
        r.timestamps_clamped=rr.timestamps_clamped_to_zero;
        return r;
    }

    if(kind==SubtitleKind::VobSub) {
        SyncResult r;
        r.kind=kind;

        std::filesystem::path in_idx=input;
        if(lower_ext(in_idx)==L".sub") in_idx.replace_extension(L".idx");
        std::filesystem::path in_sub=in_idx;
        in_sub.replace_extension(L".sub");

        const auto out_idx=default_output_path(in_idx);
        std::filesystem::path out_sub=out_idx;
        out_sub.replace_extension(L".sub");

        winterstatic::subtitle::VobSubSyncSpec vs{};
        vs.constant_offset_ms=spec.constant_offset_ms;
        vs.drift_enabled=spec.drift_enabled && drift_geometry_valid(spec);
        vs.video_a_seconds=spec.video_a_seconds;
        vs.video_b_seconds=spec.video_b_seconds;
        vs.offset_a_ms=spec.offset_a_ms;
        vs.offset_b_ms=spec.offset_b_ms;

        const auto rr=winterstatic::subtitle::rewrite_vobsub_timing(
            in_idx,in_sub,out_idx,out_sub,vs,overwrite);
        r.success=rr.success;
        r.message=rr.message;
        r.output_primary=out_idx;
        r.output_companion=out_sub;
        r.timestamps_changed=rr.idx_timestamps_changed+rr.pes_pts_changed+rr.control_delays_changed;
        r.timestamps_clamped=rr.timestamps_clamped_to_zero;
        return r;
    }

    SyncResult r;
    r.kind=kind;
    r.message="Unsupported subtitle type. Supported: SRT, ASS, SSA, VTT, PGS/SUP, VobSub IDX+SUB.";
    return r;
}

} // namespace winterstatic::subsync
