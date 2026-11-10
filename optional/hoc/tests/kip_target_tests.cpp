// Exercises the production hoc-clk target/file guard with actual temporary
// FILEs. Digest callbacks are deterministic fixtures; adapter SHA is separate.
#include "../hoc-clk/sysmodule/src/file/kip_target_core.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace kt = kip_target; namespace nh = nexus_hoc;
static unsigned checks;
static void Check(bool value, const char *name) {
    ++checks; if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
static void Hash(const void *data, std::size_t size, std::uint8_t *out) {
    auto *p = static_cast<const std::uint8_t *>(data); std::uint64_t value = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) value = (value ^ p[i]) * 1099511628211ULL;
    for (std::size_t i = 0; i < 32; ++i) { value ^= value >> 12; value ^= value << 25; value ^= value >> 27; out[i] = value & 255; }
}
static bool HashRange(FILE *f, long offset, std::size_t size, std::uint8_t *out, const kt::Io &io) {
    if (io.seek(f, offset, SEEK_SET)) return false;
    std::vector<std::uint8_t> bytes(size);
    if (io.read(bytes.data(), bytes.size(), f) != bytes.size()) return false;
    Hash(bytes.data(), bytes.size(), out); return true;
}
static void U32(std::vector<std::uint8_t> &v, std::size_t at, std::uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) v[at + i] = (x >> (8 * i)) & 255;
}
static void U64(std::vector<std::uint8_t> &v, std::size_t at, std::uint64_t x) { U32(v, at, x & UINT32_MAX); U32(v, at + 4, x >> 32); }
static std::uint32_t Read32(const std::vector<std::uint8_t> &v, std::size_t at) {
    return std::uint32_t(v[at]) | std::uint32_t(v[at+1]) << 8 | std::uint32_t(v[at+2]) << 16 | std::uint32_t(v[at+3]) << 24;
}
struct Fixture {
    std::vector<std::uint8_t> file, table;
    std::size_t offset;
    nh::Status status{};
};
static void SetStatus(Fixture &f) {
    auto &d = f.status.descriptor;
    d = {nh::Magic, nh::Abi, sizeof(d), Read32(f.table, 8), Read32(f.table, 4),
         static_cast<std::uint32_t>(f.table.size()), nh::Firmware2300, nh::SupportsPcv | nh::SupportsPtm,
         nh::DataBytes, nh::CodeBytes, nh::RuntimeBytes, nh::PayloadOffset, nh::MaxImageBytes, 0, 17, {}};
    Hash(f.table.data(), f.table.size(), d.settings_hash);
    f.status.pcv_phase = f.status.ptm_phase = nh::Committed;
    f.status.pcv_sequence = 1; f.status.ptm_sequence = 2;
    f.status.pcv_commits = f.status.ptm_commits = f.status.pcv_prepares = f.status.ptm_prepares = 1;
    f.status.pcv_module_id[0] = 1; f.status.ptm_module_id[0] = 2;
}
static Fixture Synthetic(std::size_t relative = 31) {
    Fixture f; f.file.assign(0x100 + 0x60 + 0x2000, 0xA9); f.table.assign(64, 0x7C);
    std::memset(f.file.data(), 0, 0x100); std::memcpy(f.file.data(), "KIP1", 4);
    U64(f.file, 0x10, nh::HelperProgramId);
    U32(f.file, 0x28, 0x40); U32(f.file, 0x38, 0x20);
    U32(f.file, 0x44, 0x2000); U32(f.file, 0x48, 0x2000);
    std::memcpy(f.table.data(), "CUST", 4); U32(f.table, 4, 7); U32(f.table, 8, 260);
    f.offset = 0x160 + relative; std::copy(f.table.begin(), f.table.end(), f.file.begin() + f.offset);
    SetStatus(f); return f;
}
static kt::Guard Guard(const Fixture &f) {
    kt::Guard g; g.Select(kt::Discovery::Ready, f.status, nh::Firmware2300, f.table.size(), Read32(f.table,4), Read32(f.table,8));
    Check(g.GetMode() == kt::Mode::Separated, "valid committed split status selects helper"); return g;
}
struct TempFile {
    FILE *file;
    explicit TempFile(const std::vector<std::uint8_t> &bytes) : file(std::tmpfile()) {
        Check(file != nullptr, "create local test fixture");
        Check(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && std::fflush(file) == 0, "write local fixture");
    }
    ~TempFile() { if (file) std::fclose(file); }
    std::vector<std::uint8_t> Bytes() {
        Check(!std::fseek(file, 0, SEEK_END), "seek test end");
        auto size = std::ftell(file); Check(size >= 0, "fixture length");
        std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
        Check(!std::fseek(file, 0, SEEK_SET) && std::fread(out.data(), 1, out.size(), file) == out.size(), "read fixture"); return out;
    }
};
static void Blocked(kt::Guard &g, const Fixture &f) {
    Check(g.GetMode() == kt::Mode::Blocked && g.Path() == nullptr, "failure blocks both paths");
    g.Select(kt::Discovery::Missing, f.status, nh::Firmware2300, f.table.size(), 7, 260);
    Check(g.GetMode() == kt::Mode::Blocked && !g.Path(), "failure cannot fall back to combined Loader");
}
static void DiscoveryTests() {
    const auto f = Synthetic(); kt::Guard legacy;
    legacy.Select(kt::Discovery::Missing, {}, nh::Firmware2300, f.table.size(), 7, 260);
    Check(legacy.GetMode() == kt::Mode::Blocked && legacy.Path() == nullptr, "absent helper blocks legacy KIP path on USB-only boot");
    legacy.Select(kt::Discovery::Ready, f.status, nh::Firmware2300, f.table.size(), 7, 260);
    Check(legacy.GetMode() == kt::Mode::Blocked, "one-time target selection never enables clocks after an absent helper");
    kt::Guard error; error.Select(kt::Discovery::Error, f.status, nh::Firmware2300, f.table.size(), 7, 260); Blocked(error,f);
    for (unsigned mutation = 0; mutation < 17; ++mutation) {
        auto s = f.status;
        switch(mutation) {
            case 0: ++s.descriptor.abi; break; case 1: s.descriptor.cust_size++; break;
            case 2: s.descriptor.cust_revision++; break; case 3: s.descriptor.engine_version++; break;
            case 4: s.reserved=1; break; case 5: s.last_error=2; break;
            case 6: s.pcv_phase=nh::Prepared; break; case 7: s.ptm_phase=nh::Failed; break;
            case 8: s.pcv_sequence=0; break; case 9: s.ptm_sequence=0; break;
            case 10: s.pcv_commits=0; break; case 11: s.ptm_commits=0; break;
            case 12: std::memset(s.pcv_module_id,0,32); break; case 13: std::memset(s.ptm_module_id,0,32); break;
            case 14: std::memset(s.descriptor.settings_hash,0,32); break;
            case 15: s.descriptor.firmware=nh::Firmware2301; break; case 16: s.descriptor.generation=0; break;
        }
        kt::Guard g; g.Select(kt::Discovery::Ready,s,nh::Firmware2300,f.table.size(),7,260); Blocked(g,f);
    }
}
static void HappyFile(const Fixture &f) {
    auto g=Guard(f); Check(std::strcmp(g.Path(),kt::SeparatedPath)==0,"separate path selected");
    TempFile file(f.file); std::vector<std::uint8_t> read(f.table.size());
    Check(g.ReadTable(file.file,read.data(),read.size(),Hash) && read==f.table,"actual helper table read");
    auto changed=f.table; changed[20]^=1;
    Check(g.WriteTable(file.file,changed.data(),changed.size(),Hash,HashRange),"guarded save succeeds");
    auto expected=f.file; std::copy(changed.begin(),changed.end(),expected.begin()+f.offset);
    Check(file.Bytes()==expected,"save modifies exactly CUST bytes only");
    Check(g.ReadTable(file.file,read.data(),read.size(),Hash) && read==changed,"accepted hash advances after verified save");
    Check(g.WriteTable(file.file,f.table.data(),f.table.size(),Hash,HashRange),"second verified save accepted");
    Check(file.Bytes()==f.file,"second save restores exact fixture");
    std::fseek(file.file,static_cast<long>(f.offset+22),SEEK_SET); std::fputc(0x39,file.file); std::fflush(file.file);
    const auto external=file.Bytes();
    Check(!g.WriteTable(file.file,changed.data(),changed.size(),Hash,HashRange),"external CUST modification refused");
    Check(file.Bytes()==external,"failed hash gate writes nothing"); Blocked(g,f);
}
static void MalformedFiles() {
    auto f=Synthetic(); std::vector<std::uint8_t> table(f.table.size());
    auto nofile=Guard(f); Check(!nofile.ReadTable(nullptr,table.data(),table.size(),Hash),"missing selected helper file blocked"); Blocked(nofile,f);
    for (unsigned mutation=0; mutation<12; ++mutation) {
        auto bad=f.file;
        switch(mutation) {
            case 0: bad[0]='X'; break; case 1: U64(bad,0x10,nh::LoaderProgramId); break;
            case 2: bad[0x1f]|=4; break; case 3: U32(bad,0x44,0x1FFF); break;
            case 4: U32(bad,0x48,32); break; case 5: U32(bad,0x28,UINT32_MAX); break;
            case 6: bad.resize(bad.size()-1); break; case 7: std::memcpy(bad.data()+f.offset+100,"CUST",4); break;
            case 8: U32(bad,f.offset+4,8); break; case 9: U32(bad,f.offset+8,261); break;
            case 10: bad[f.offset+20]^=2; break; case 11: bad.resize(0x80); break;
        }
        auto g=Guard(f); TempFile file(bad);
        Check(!g.ReadTable(file.file,table.data(),table.size(),Hash),"malformed/mismatched file refused"); Blocked(g,f);
        Check(file.Bytes()==bad,"read validation never writes fixture");
    }
    auto g=Guard(f); TempFile file(f.file); auto wrong=f.table; wrong[0]='X';
    Check(!g.WriteTable(file.file,wrong.data(),wrong.size(),Hash,HashRange),"new table identity cannot change");
    Check(file.Bytes()==f.file,"invalid new schema writes nothing"); Blocked(g,f);
}
enum class Fault { None, Read, ShortWrite, Seek, Tell, Flush, Readback };
static Fault fault; static unsigned writes;
static std::size_t ReadFault(void*p,std::size_t n,FILE*f){if(fault==Fault::Read)return 0; auto r=kt::Read(p,n,f);if(fault==Fault::Readback&&writes&&r)static_cast<std::uint8_t*>(p)[0]^=1;return r;}
static std::size_t WriteFault(const void*p,std::size_t n,FILE*f){++writes;return kt::Write(p,fault==Fault::ShortWrite?n-1:n,f);}
static int SeekFault(FILE*f,long n,int w){return fault==Fault::Seek?-1:std::fseek(f,n,w);}
static long TellFault(FILE*f){return fault==Fault::Tell?-1:std::ftell(f);}
static int FlushFault(FILE*f){auto result=std::fflush(f);return fault==Fault::Flush?-1:result;}
static const kt::Io FaultIo{ReadFault,WriteFault,SeekFault,TellFault,FlushFault};
static void IoTests() {
    const auto f=Synthetic(); auto changed=f.table;changed[20]^=1;
    for(auto mode:{Fault::Read,Fault::ShortWrite,Fault::Seek,Fault::Tell,Fault::Flush,Fault::Readback}){
        auto g=Guard(f);TempFile file(f.file);fault=mode;writes=0;
        Check(!g.WriteTable(file.file,changed.data(),changed.size(),Hash,HashRange,FaultIo),"I/O failure cannot acknowledge saved settings");Blocked(g,f);
        if(mode==Fault::Read||mode==Fault::Seek||mode==Fault::Tell)Check(writes==0&&file.Bytes()==f.file,"prewrite failure writes nothing");
        fault=Fault::None;
        Check(!g.WriteTable(file.file,changed.data(),changed.size(),Hash,HashRange),"failed save cannot retry through stale guard");
    }
}
static Fixture ActualFixture(const char *path) {
    FILE*f=std::fopen(path,"rb");Check(f!=nullptr,"open staged helper read-only");
    std::fseek(f,0,SEEK_END);const auto length=std::ftell(f);Check(length>0,"staged helper length");std::rewind(f);
    Fixture result;result.file.resize(length);Check(std::fread(result.file.data(),1,result.file.size(),f)==result.file.size(),"read staged helper");std::fclose(f);
    const std::uint64_t start=0x100ULL+Read32(result.file,0x28)+Read32(result.file,0x38);
    const std::uint64_t size=Read32(result.file,0x48);
    Check(start+size<=result.file.size(),"staged data segment bounds");
    result.offset=0;
    for(std::uint64_t at=start;at+26824<=start+size;++at)if(std::memcmp(result.file.data()+at,"CUST",4)==0){Check(result.offset==0,"single actual table");result.offset=at;}
    Check(result.offset!=0,"actual CUST table exists");
    result.table.assign(result.file.begin()+result.offset,result.file.begin()+result.offset+26824);SetStatus(result);return result;
}
int main(int argc,char**argv){
    DiscoveryTests();MalformedFiles();IoTests();HappyFile(Synthetic());
    for(std::size_t offset=0;offset<1025;++offset){auto f=Synthetic(offset);auto g=Guard(f);TempFile file(f.file);std::vector<std::uint8_t>table(f.table.size());Check(g.ReadTable(file.file,table.data(),table.size(),Hash)&&table==f.table,"CUST marker across every 512-byte chunk position");}
    if(argc==2)HappyFile(ActualFixture(argv[1]));else Check(false,"actual staged KIP fixture argument required");
    std::cout<<checks<<" KIP-target guard checks passed\n";
}
