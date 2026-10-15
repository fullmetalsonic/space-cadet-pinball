#include "pch.h"
#include "hq_render.h"
#include "gdrv.h"
#include "zdrv.h"
#include "winmain.h"
#include "options.h"
#include "pb.h"
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using Digest = std::array<uint8_t, 32>;
class ShaState
{
    uint32_t H[8]{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t Buffer[64]{};
    uint64_t Bytes=0;
    size_t Used=0;
    static uint32_t Rotate(uint32_t n,unsigned bits) { return (n>>bits)|(n<<(32-bits)); }
    void Block()
    {
        static const uint32_t K[64]{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for(unsigned i=0;i<16;++i) w[i]=(uint32_t(Buffer[i*4])<<24)|(uint32_t(Buffer[i*4+1])<<16)|(uint32_t(Buffer[i*4+2])<<8)|Buffer[i*4+3];
        for(unsigned i=16;i<64;++i) {
            auto a=Rotate(w[i-15],7)^Rotate(w[i-15],18)^(w[i-15]>>3);
            auto b=Rotate(w[i-2],17)^Rotate(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+a+w[i-7]+b;
        }
        auto a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
        for(unsigned i=0;i<64;++i) {
            auto s1=Rotate(e,6)^Rotate(e,11)^Rotate(e,25);
            auto t1=h+s1+((e&f)^(~e&g))+K[i]+w[i];
            auto s0=Rotate(a,2)^Rotate(a,13)^Rotate(a,22);
            auto t2=s0+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=d;H[4]+=e;H[5]+=f;H[6]+=g;H[7]+=h;
    }
public:
    void Add(const void* data,size_t length) {
        auto p=static_cast<const uint8_t*>(data);Bytes+=length;
        while(length) {
            auto count=std::min(length,64-Used);std::memcpy(Buffer+Used,p,count);Used+=count;p+=count;length-=count;
            if(Used==64) {Block();Used=0;}
        }
    }
    Digest Finish() {
        const auto bits=Bytes*8;uint8_t one=0x80,zero=0;Add(&one,1);
        while(Used!=56) Add(&zero,1);
        uint8_t length[8];for(unsigned i=0;i<8;++i)length[7-i]=uint8_t(bits>>(i*8));Add(length,8);
        Digest result{};for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)result[i*4+j]=uint8_t(H[i]>>(24-j*8));return result;
    }
};
struct Surface { int Width=0,Height=0;std::vector<ColorRgba> Pixels; };
struct OverlayCommand {
    gdrv_bitmap8* Destination;gdrv_bitmap8* Source;
    int Width,Height,X,Y,SourceX,SourceY;ColorRgba Color;bool Transparent,Fill;
};
struct Overlay { uint64_t Order;std::vector<OverlayCommand> Commands; };
struct Capture { const void* Owner;std::vector<OverlayCommand> Commands; };
std::map<gdrv_bitmap8*,Surface> surfaces,assets;
std::set<gdrv_bitmap8*> targets;
std::vector<hq_render::Binding> bindings;
std::map<const void*,Overlay> overlays;
std::vector<Capture> captures;
std::vector<uint16_t> depths;
std::string dataPath,packPath,error;
gdrv_bitmap8* screen=nullptr;
zmap_header_type* screenDepth=nullptr;
SDL_Texture* texture=nullptr;
SDL_Renderer* textureRenderer=nullptr;
int scale=1;
bool enabled=false,replaying=false,captureFailed=false,measure=false;
uint64_t overlayOrder=0;
double compositionMs=0;
std::vector<hq_render::FrameSample> samples;
struct Timing {
    Uint64 Started;
    Timing():Started(measure?SDL_GetPerformanceCounter():0){}
    ~Timing(){if(Started)compositionMs+=1000.0*double(SDL_GetPerformanceCounter()-Started)/SDL_GetPerformanceFrequency();}
};
void DestroyTexture(){if(texture)SDL_DestroyTexture(texture);texture=nullptr;textureRenderer=nullptr;}
void Disable(const char* reason) {
    enabled=false;scale=1;surfaces.clear();assets.clear();depths.clear();DestroyTexture();
    if(reason){error=reason;SDL_Log("PINBALL_HQ %s",reason);}
}
bool Rect(int width,int height,int x,int y,int limitX,int limitY) {
    return width>=0&&height>=0&&x>=0&&y>=0&&width<=limitX&&height<=limitY&&x<=limitX-width&&y<=limitY-height;
}
Surface* Target(gdrv_bitmap8* bitmap) {
    if(!enabled||!bitmap||targets.find(bitmap)==targets.end())return nullptr;
    auto found=surfaces.find(bitmap);if(found!=surfaces.end())return &found->second;
    Surface result;result.Width=bitmap->Width*scale;result.Height=bitmap->Height*scale;
    const auto size=uint64_t(result.Width)*result.Height;
    if(size>64*1024*1024)throw std::runtime_error("HQ canvas size");
    result.Pixels.resize(static_cast<size_t>(size),ColorRgba(0));
    // Only the screen contains initialized source pixels. Ball backup rectangles are written before use.
    if(bitmap==screen)for(int y=0;y<result.Height;++y)for(int x=0;x<result.Width;++x)
        result.Pixels[y*result.Width+x]=bitmap->BmpBufPtr1[(y/scale)*bitmap->Stride+x/scale];
    return &surfaces.emplace(bitmap,std::move(result)).first->second;
}
ColorRgba Source(gdrv_bitmap8* bitmap,int x,int y) {
    auto target=surfaces.find(bitmap);
    if(target!=surfaces.end())return target->second.Pixels[y*target->second.Width+x];
    auto asset=assets.find(bitmap);
    if(asset!=assets.end())return asset->second.Pixels[y*asset->second.Width+x];
    return bitmap->BmpBufPtr1[(y/scale)*bitmap->Stride+x/scale];
}
void CaptureCommand(const OverlayCommand& command) {
    if(replaying||captureFailed||command.Destination!=screen)return;
    try {
        for(auto& capture:captures) {
            if(capture.Commands.size()>=2048)throw std::runtime_error("HQ overlay command limit");
            capture.Commands.push_back(command);
        }
    } catch(...) {captureFailed=true;captures.clear();overlays.clear();Disable("HQ overlay allocation failed");}
}
std::vector<uint8_t> ReadFile(const std::string& path,size_t limit) {
    // Match the DAT loader's UTF-8/CWD semantics, including Android's versioned asset directory.
    std::unique_ptr<FILE,decltype(&fclose)> file(fopenu(path.c_str(),"rb"),fclose);
    if(!file)throw std::runtime_error("HQ data unavailable");
    if(fseek(file.get(),0,SEEK_END)!=0)throw std::runtime_error("HQ data length");
    const auto size=ftell(file.get());
    if(size<0||uint64_t(size)>limit||fseek(file.get(),0,SEEK_SET)!=0)throw std::runtime_error("HQ data length");
    std::vector<uint8_t> result(static_cast<size_t>(size));
    const auto read=result.empty()?0:fread(result.data(),1,result.size(),file.get());
    if(read!=result.size()||ferror(file.get()))throw std::runtime_error("HQ data read");return result;
}
Digest BitmapHash(const gdrv_bitmap8& bitmap) {
    ShaState sha;
    for(int y=0;y<bitmap.Height;++y)for(int x=0;x<bitmap.Width;++x) {
        const auto color=bitmap.BmpBufPtr1[y*bitmap.Stride+x].Color;
        const uint8_t bytes[]{uint8_t(color),uint8_t(color>>8),uint8_t(color>>16),uint8_t(color>>24)};sha.Add(bytes,4);
    }
    return sha.Finish();
}
struct Reader {
    const std::vector<uint8_t>& Bytes;size_t Position=0,End;
    Reader(const std::vector<uint8_t>& bytes,size_t position,size_t end):Bytes(bytes),Position(position),End(end){}
    void Need(size_t length)const{if(Position>End||length>End-Position)throw std::runtime_error("HQ truncated pack");}
    uint32_t U32(){Need(4);uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(Bytes[Position++])<<(i*8);return v;}
    int32_t I32(){return static_cast<int32_t>(U32());}
    uint64_t U64(){auto a=U32();return uint64_t(a)|(uint64_t(U32())<<32);}
    Digest Hash(){Need(32);Digest d{};std::memcpy(d.data(),Bytes.data()+Position,32);Position+=32;return d;}
};
bool Load(const std::string& path,bool complete) {
    try {
        if(!screen||!screenDepth||captureFailed)throw std::runtime_error("HQ renderer unavailable");
        auto bytes=ReadFile(path,256*1024*1024);
        const uint8_t magic[]{'P','B','H','Q','v','1',0,0};
        if(bytes.size()<80||std::memcmp(bytes.data(),magic,8))throw std::runtime_error("HQ pack format");
        Digest footer{};std::memcpy(footer.data(),bytes.data()+bytes.size()-32,32);
        if(hq_render::Sha256(bytes.data(),bytes.size()-32)!=footer)throw std::runtime_error("HQ pack checksum");
        Reader reader{bytes,8,bytes.size()-32};const auto newScale=reader.U32(),count=reader.U32();
        if((newScale!=1&&newScale!=2)||count>4096)throw std::runtime_error("HQ pack limits");
        const auto sourceHash=reader.Hash();auto source=ReadFile(dataPath,64*1024*1024);
        if(hq_render::Sha256(source.data(),source.size())!=sourceHash)throw std::runtime_error("HQ source identity");
        std::map<std::pair<int,unsigned>,gdrv_bitmap8*> expected;
        std::set<gdrv_bitmap8*> expectedBitmaps;
        for(const auto& b:bindings)if(b.Bitmap) {
            if(!expected.emplace(std::make_pair(b.Group,b.Resolution),b.Bitmap).second||
                !expectedBitmaps.insert(b.Bitmap).second)throw std::runtime_error("HQ duplicate source binding");
        }
        if(complete&&count!=expected.size())throw std::runtime_error("HQ incomplete set");
        std::map<gdrv_bitmap8*,Surface> candidate;
        std::set<std::pair<int,unsigned>> seen;
        uint64_t colorBytes=0;
        for(uint32_t i=0;i<count;++i) {
            auto group=reader.I32();auto resolution=reader.U32();auto width=reader.U32(),height=reader.U32();
            auto x=reader.I32(),y=reader.I32();auto original=reader.Hash();auto payload=reader.U64();
            auto key=std::make_pair(group,resolution);auto match=expected.find(key);
            if(match==expected.end()||!seen.insert(key).second||resolution>2||width>8192||height>8192)throw std::runtime_error("HQ bitmap key");
            auto bitmap=match->second;
            if(bitmap->Width!=int(width)||bitmap->Height!=int(height)||bitmap->XPosition!=x||bitmap->YPosition!=y||BitmapHash(*bitmap)!=original)throw std::runtime_error("HQ bitmap identity");
            const uint64_t pixels=uint64_t(width)*height*newScale*newScale;
            if(payload!=pixels*3||payload>reader.End-reader.Position)throw std::runtime_error("HQ RGB length");
            colorBytes+=pixels*4;if(colorBytes>256*1024*1024)throw std::runtime_error("HQ decoded budget");
            Surface asset;asset.Width=int(width*newScale);asset.Height=int(height*newScale);asset.Pixels.resize(static_cast<size_t>(pixels));
            for(int ay=0;ay<asset.Height;++ay)for(int ax=0;ax<asset.Width;++ax) {
                auto p=bytes.data()+reader.Position;reader.Position+=3;
                auto originalColor=bitmap->BmpBufPtr1[(ay/newScale)*bitmap->Stride+ax/newScale];
                asset.Pixels[ay*asset.Width+ax]=originalColor.Color?ColorRgba(p[0],p[1],p[2],originalColor.GetAlpha()):ColorRgba(0);
            }
            if(!candidate.emplace(bitmap,std::move(asset)).second)throw std::runtime_error("HQ duplicate bitmap");
        }
        if(reader.Position!=reader.End||(complete&&seen.size()!=expected.size()))throw std::runtime_error("HQ pack trailing or missing data");
        // Commit only after every source and payload passes. No partial candidate is presented.
        surfaces.clear();assets=std::move(candidate);scale=int(newScale);enabled=true;error.clear();
        depths.resize(size_t(screenDepth->Width)*scale*screenDepth->Height*scale);
        for(int dy=0;dy<screenDepth->Height*scale;++dy)for(int dx=0;dx<screenDepth->Width*scale;++dx)
            depths[dy*screenDepth->Width*scale+dx]=screenDepth->ZPtr1[(dy/scale)*screenDepth->Stride+dx/scale];
        Target(screen);DestroyTexture();return true;
    } catch(const std::exception& e){Disable(e.what());return false;}
      catch(...){Disable("HQ allocation failed");return false;}
}
}

std::array<uint8_t,32> hq_render::Sha256(const void* data,size_t length){ShaState state;state.Add(data,length);return state.Finish();}
std::string hq_render::Sha256Hex(const void* data,size_t length){auto digest=Sha256(data,length);static const char digits[]="0123456789abcdef";std::string text;for(auto c:digest){text+=digits[c>>4];text+=digits[c&15];}return text;}
void hq_render::Initialize(gdrv_bitmap8* canvas,zmap_header_type* depth,gdrv_bitmap8* const* backups,size_t count,
    const std::vector<Binding>& sourceBindings,const std::string& sourceData,const std::string& sourcePack) {
    Shutdown();
    try {screen=canvas;screenDepth=depth;dataPath=sourceData;packPath=sourcePack;bindings=sourceBindings;
        targets.insert(canvas);for(size_t i=0;i<count;++i)targets.insert(backups[i]);}
    catch(...) {Shutdown();Disable("HQ renderer allocation failed");}
}
void hq_render::Shutdown(){Disable(nullptr);targets.clear();bindings.clear();overlays.clear();captures.clear();screen=nullptr;screenDepth=nullptr;dataPath.clear();packPath.clear();captureFailed=false;replaying=false;overlayOrder=0;error.clear();}
bool hq_render::Enable(bool value){if(!value){Disable(nullptr);error.clear();return true;}return Load(packPath,true);}
bool hq_render::LoadPackForTest(const std::string& path,bool complete){return Load(path,complete);}
bool hq_render::Enabled(){return enabled;}
int hq_render::Scale(){return enabled?scale:1;}
const std::string& hq_render::Error(){return error;}
hq_render::OverlayScope::OverlayScope(const void* owner):Capturing(false){if(!owner||captureFailed||replaying||!screen)return;try{captures.push_back(Capture{owner,{}});Capturing=true;}catch(...){captureFailed=true;Disable("HQ overlay allocation failed");}}
hq_render::OverlayScope::~OverlayScope(){if(!Capturing||captures.empty()||captureFailed)return;try{auto capture=std::move(captures.back());captures.pop_back();if(overlays.size()>=256&&overlays.find(capture.Owner)==overlays.end())throw std::runtime_error("HQ overlay owner limit");overlays[capture.Owner]=Overlay{++overlayOrder,std::move(capture.Commands)};}catch(...){captureFailed=true;captures.clear();overlays.clear();Disable("HQ overlay allocation failed");}}
void hq_render::ForgetOverlay(const void* owner){overlays.erase(owner);}
void hq_render::ForgetBitmap(gdrv_bitmap8* bitmap)
{
    surfaces.erase(bitmap);assets.erase(bitmap);targets.erase(bitmap);
    const auto remaining=std::remove_if(bindings.begin(),bindings.end(),
        [bitmap](const Binding& binding){return binding.Bitmap==bitmap;});
    const bool removedSource=remaining!=bindings.end();
    bindings.erase(remaining,bindings.end());
    for(auto it=overlays.begin();it!=overlays.end();) {
        bool remove=it->first==bitmap;
        for(const auto& command:it->second.Commands)
            remove|=command.Source==bitmap||command.Destination==bitmap;
        if(remove)it=overlays.erase(it);else ++it;
    }
    for(auto& capture:captures)
        capture.Commands.erase(std::remove_if(capture.Commands.begin(),capture.Commands.end(),
            [bitmap](const OverlayCommand& command){return command.Source==bitmap||command.Destination==bitmap;}),capture.Commands.end());
    if(bitmap==screen)Shutdown();
    else if(removedSource&&enabled)Disable("HQ source bitmap unloaded");
}
void hq_render::ForgetDepth(zmap_header_type* depth){if(depth==screenDepth)Shutdown();}
void hq_render::Fill(gdrv_bitmap8* bitmap,int w,int h,int x,int y,ColorRgba color){
    CaptureCommand(OverlayCommand{bitmap,nullptr,w,h,x,y,0,0,color,false,true});if(!enabled)return;Timing time;
    try{auto dst=Target(bitmap);if(!dst)return;if(!Rect(w,h,x,y,bitmap->Width,bitmap->Height))throw std::runtime_error("HQ fill rectangle");for(int dy=y*scale;dy<(y+h)*scale;++dy)std::fill(dst->Pixels.begin()+dy*dst->Width+x*scale,dst->Pixels.begin()+dy*dst->Width+(x+w)*scale,color);}catch(...){Disable("HQ fill failed");}
}
void hq_render::Copy(gdrv_bitmap8* destination,int w,int h,int x,int y,gdrv_bitmap8* source,int sx,int sy,bool transparent){
    CaptureCommand(OverlayCommand{destination,source,w,h,x,y,sx,sy,ColorRgba(0),transparent,false});if(!enabled)return;Timing time;
    try{auto dst=Target(destination);if(!dst)return;if(!source||!Rect(w,h,x,y,destination->Width,destination->Height)||!Rect(w,h,sx,sy,source->Width,source->Height))throw std::runtime_error("HQ copy rectangle");
        for(int dy=0;dy<h*scale;++dy)for(int dx=0;dx<w*scale;++dx){auto color=Source(source,sx*scale+dx,sy*scale+dy);if(!transparent||source->BmpBufPtr1[(sy+dy/scale)*source->Stride+sx+dx/scale].Color)dst->Pixels[(y*scale+dy)*dst->Width+x*scale+dx]=color;}
    }catch(...){Disable("HQ copy failed");}
}
void hq_render::FillDepth(zmap_header_type* depth,int w,int h,int x,int y,uint16_t value){if(!enabled||depth!=screenDepth)return;Timing time;try{if(!Rect(w,h,x,y,depth->Width,depth->Height))throw std::runtime_error("HQ depth rectangle");const int stride=depth->Width*scale;for(int dy=y*scale;dy<(y+h)*scale;++dy)std::fill(depths.begin()+dy*stride+x*scale,depths.begin()+dy*stride+(x+w)*scale,value);}catch(...){Disable("HQ depth fill failed");}}
void hq_render::Paint(int w,int h,gdrv_bitmap8* destination,int x,int y,zmap_header_type* dstDepth,int zx,int zy,gdrv_bitmap8* source,int sx,int sy,zmap_header_type* srcDepth,int szx,int szy){
    if(!enabled)return;Timing time;try{auto dst=Target(destination);if(!dst)return;if(dstDepth!=screenDepth||!source||!srcDepth||!Rect(w,h,x,y,destination->Width,destination->Height)||!Rect(w,h,sx,sy,source->Width,source->Height)||!Rect(w,h,zx,zy,dstDepth->Width,dstDepth->Height)||!Rect(w,h,szx,szy,srcDepth->Width,srcDepth->Height))throw std::runtime_error("HQ paint rectangle");
        for(int dy=0;dy<h*scale;++dy)for(int dx=0;dx<w*scale;++dx){auto& depth=depths[(zy*scale+dy)*dstDepth->Width*scale+zx*scale+dx];auto sourceDepth=srcDepth->ZPtr1[(szy+dy/scale)*srcDepth->Stride+szx+dx/scale];if(depth>=sourceDepth){dst->Pixels[(y*scale+dy)*dst->Width+x*scale+dx]=Source(source,sx*scale+dx,sy*scale+dy);depth=sourceDepth;}}
    }catch(...){Disable("HQ sprite paint failed");}
}
void hq_render::PaintFlat(int w,int h,gdrv_bitmap8* destination,int x,int y,zmap_header_type* depth,int zx,int zy,gdrv_bitmap8* source,int sx,int sy,uint16_t value){
    if(!enabled)return;Timing time;try{auto dst=Target(destination);if(!dst)return;if(depth!=screenDepth||!source||!Rect(w,h,x,y,destination->Width,destination->Height)||!Rect(w,h,sx,sy,source->Width,source->Height)||!Rect(w,h,zx,zy,depth->Width,depth->Height))throw std::runtime_error("HQ ball rectangle");
        for(int dy=0;dy<h*scale;++dy)for(int dx=0;dx<w*scale;++dx){auto color=Source(source,sx*scale+dx,sy*scale+dy);if(source->BmpBufPtr1[(sy+dy/scale)*source->Stride+sx+dx/scale].Color&&depths[(zy*scale+dy)*depth->Width*scale+zx*scale+dx]>value)dst->Pixels[(y*scale+dy)*dst->Width+x*scale+dx]=color;}
    }catch(...){Disable("HQ ball paint failed");}
}
void hq_render::Scroll(gdrv_bitmap8* bitmap,int offset){if(!enabled)return;try{auto dst=Target(bitmap);if(!dst)return;if(std::abs(offset)>bitmap->Width)throw std::runtime_error("HQ scroll rectangle");const int start=std::max(0,-offset)*scale,end=std::max(0,offset)*scale,length=(bitmap->Width-std::abs(offset))*scale;for(int y=0;y<dst->Height;++y)std::memmove(dst->Pixels.data()+y*dst->Width+end,dst->Pixels.data()+y*dst->Width+start,length*sizeof(ColorRgba));}catch(...){Disable("HQ scroll failed");}}
void hq_render::BeginRebuild(){if(!enabled)return;Fill(screen,screen->Width,screen->Height,0,0,ColorRgba(0));FillDepth(screenDepth,screenDepth->Width,screenDepth->Height,0,0,0xffff);}
void hq_render::ReplayOverlays(){if(!enabled)return;try{std::vector<const Overlay*> ordered;for(const auto& entry:overlays)ordered.push_back(&entry.second);std::sort(ordered.begin(),ordered.end(),[](const Overlay* a,const Overlay* b){return a->Order<b->Order;});replaying=true;for(auto overlay:ordered)for(const auto& command:overlay->Commands){if(command.Fill)Fill(command.Destination,command.Width,command.Height,command.X,command.Y,command.Color);else Copy(command.Destination,command.Width,command.Height,command.X,command.Y,command.Source,command.SourceX,command.SourceY,command.Transparent);}replaying=false;}catch(...){replaying=false;Disable("HQ overlay replay failed");}}
void hq_render::RecreateTexture(){DestroyTexture();}
SDL_Texture* hq_render::UploadTexture(){
    if(!enabled)return nullptr;const auto start=measure?SDL_GetPerformanceCounter():0;
    try{auto pixels=Target(screen);if(!texture||textureRenderer!=winmain::Renderer){DestroyTexture();UsingSdlHint hint{SDL_HINT_RENDER_SCALE_QUALITY,options::Options.LinearFiltering?"linear":"nearest"};texture=SDL_CreateTexture(winmain::Renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,pixels->Width,pixels->Height);if(!texture)throw std::runtime_error("HQ texture unavailable");textureRenderer=winmain::Renderer;SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);}
        if(SDL_UpdateTexture(texture,nullptr,pixels->Pixels.data(),pixels->Width*sizeof(ColorRgba))!=0)throw std::runtime_error("HQ texture upload failed");
        if(measure){if(samples.size()<8192)samples.push_back(FrameSample{compositionMs,1000.0*double(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()});compositionMs=0;}
        return texture;
    }catch(...){Disable("HQ texture failed");return nullptr;}
}
void hq_render::Measure(bool value){measure=value;compositionMs=0;samples.clear();}
const std::vector<hq_render::FrameSample>& hq_render::Samples(){return samples;}
const std::vector<ColorRgba>* hq_render::ScreenPixelsForTest(){auto found=surfaces.find(screen);return found==surfaces.end()?nullptr:&found->second.Pixels;}
const std::vector<uint16_t>* hq_render::DepthPixelsForTest(){return enabled?&depths:nullptr;}
