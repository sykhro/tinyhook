#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include "th/selection.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <fstream>
#include <iostream>

@interface THPicker : NSObject {
@public
    NSArray<NSDictionary*>* windows;
    NSDictionary* selected;
    NSMutableArray<NSWindow*>* panels;
    NSTimer* timer;
    BOOL cancelled;
}
- (void)refresh;
- (void)finish:(BOOL)cancel;
@end

@interface THPickerView : NSView
@property(nonatomic,weak) THPicker* picker;
@end

@interface THPickerWindow : NSPanel
@end
@implementation THPickerWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

static NSRect windowRect(NSDictionary* window) {
    CGRect bounds{};
    if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(id)kCGWindowBounds],&bounds))return NSZeroRect;
    const auto top=NSMaxY(NSScreen.screens.firstObject.frame);
    return NSMakeRect(bounds.origin.x,top-CGRectGetMaxY(bounds),bounds.size.width,bounds.size.height);
}

@implementation THPickerView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)resetCursorRects { [self addCursorRect:self.bounds cursor:NSCursor.crosshairCursor]; }
- (void)keyDown:(NSEvent*)event {
    if(event.keyCode==53)[self.picker finish:YES];
}
- (void)rightMouseDown:(NSEvent*)event { (void)event; [self.picker finish:YES]; }
- (void)mouseDown:(NSEvent*)event {
    (void)event;
    [self.picker refresh];
    if(self.picker->selected)[self.picker finish:NO];
    else NSBeep();
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithWhite:0 alpha:0.12] setFill];
    NSRectFill(self.bounds);
    auto selected=self.picker->selected;
    if(selected){
        NSRect rect=NSOffsetRect(windowRect(selected),-self.window.frame.origin.x,-self.window.frame.origin.y);
        [[NSColor colorWithCalibratedRed:0.2 green:0.6 blue:1 alpha:0.2] setFill];
        NSRectFill(rect);
        NSBezierPath* border=[NSBezierPath bezierPathWithRect:NSInsetRect(rect,1.5,1.5)];
        border.lineWidth=3;
        [NSColor.controlAccentColor setStroke];
        [border stroke];
    }
    NSString* title=selected[(id)kCGWindowName];
    NSString* label=selected?(title.length?title:@"Untitled window"):@"Point at a game window";
    NSRect hud=NSMakeRect(NSMidX(self.bounds)-230,36,460,64);
    [[NSColor colorWithWhite:0.1 alpha:0.94] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:hud xRadius:12 yRadius:12] fill];
    NSMutableParagraphStyle* style=[[NSMutableParagraphStyle alloc] init];
    style.alignment=NSTextAlignmentCenter;
    style.lineBreakMode=NSLineBreakByTruncatingMiddle;
    [label drawInRect:NSInsetRect(NSOffsetRect(hud,0,10),16,10) withAttributes:@{
        NSFontAttributeName:[NSFont systemFontOfSize:14 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName:NSColor.whiteColor,NSParagraphStyleAttributeName:style}];
    [@"Click to attach  ·  Esc to cancel" drawInRect:NSMakeRect(hud.origin.x+16,hud.origin.y+9,428,18) withAttributes:@{
        NSFontAttributeName:[NSFont systemFontOfSize:12],NSForegroundColorAttributeName:NSColor.lightGrayColor,
        NSParagraphStyleAttributeName:style}];
}
@end

@implementation THPicker
- (instancetype)init {
    if((self=[super init]))panels=[NSMutableArray array];
    return self;
}
- (void)refresh {
    windows=CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly|kCGWindowListExcludeDesktopElements,kCGNullWindowID));
    selected=nil;
    const NSPoint mouse=NSEvent.mouseLocation;
    for(NSDictionary* window in windows){
        const auto owner=[window[(id)kCGWindowOwnerPID] intValue];
        if(owner==getpid())continue;
        const auto layer=[window[(id)kCGWindowLayer] integerValue];
        if(layer<0)continue;
        // Wine raises fullscreen game windows above the normal application layer.
        // Exclude system surfaces such as the cursor, even when they have a title.
        if(layer!=0){
            NSRunningApplication* app=[NSRunningApplication runningApplicationWithProcessIdentifier:owner];
            if(![window[(id)kCGWindowName] length]||!app||app.activationPolicy==NSApplicationActivationPolicyProhibited)continue;
        }
        NSRect rect=windowRect(window);
        if(rect.size.width<1||rect.size.height<1)continue;
        if(NSPointInRect(mouse,rect)){selected=window;break;}
    }
    for(NSWindow* window in panels)[window.contentView setNeedsDisplay:YES];
}
- (void)finish:(BOOL)cancel {
    cancelled=cancel;
    [NSApp stop:nil];
}
@end

namespace {
    using namespace th;

    std::string pick_window() {
        if(!CGPreflightScreenCaptureAccess()&&!CGRequestScreenCaptureAccess())
            throw Error("Allow Screen Recording for your terminal in System Settings, then run th-pick again.");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        THPicker* picker=[[THPicker alloc] init];
        for(NSScreen* screen in NSScreen.screens){
            THPickerWindow* panel=[[THPickerWindow alloc] initWithContentRect:screen.frame styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
            panel.opaque=NO;
            panel.backgroundColor=NSColor.clearColor;
            panel.hasShadow=NO;
            panel.hidesOnDeactivate=NO;
            panel.worksWhenModal=YES;
            panel.level=CGWindowLevelForKey(kCGMaximumWindowLevelKey);
            panel.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary|
                NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorIgnoresCycle;
            if(@available(macOS 13.0,*))panel.collectionBehavior|=NSWindowCollectionBehaviorCanJoinAllApplications;
            THPickerView* view=[[THPickerView alloc] initWithFrame:NSMakeRect(0,0,screen.frame.size.width,screen.frame.size.height)];
            view.picker=picker;
            panel.contentView=view;
            [panel makeFirstResponder:view];
            [picker->panels addObject:panel];
            [panel orderFrontRegardless];
        }
        [picker refresh];
        [picker->panels.firstObject makeKeyWindow];
        picker->timer=[NSTimer scheduledTimerWithTimeInterval:0.05 target:picker selector:@selector(refresh) userInfo:nil repeats:YES];
        [NSApp run];
        [picker->timer invalidate];
        for(NSWindow* panel in picker->panels)[panel orderOut:nil];
        if(picker->cancelled)return {};
        NSString* title=picker->selected[(id)kCGWindowName];
        if(!title.length)throw Error("This window has no title. Choose a titled game window.");
        return title.UTF8String;
    }

    struct Socket {
        int fd=-1;
        ~Socket(){if(fd>=0)close(fd);}
    };

    void transfer(int fd,void* data,std::size_t size,bool sending) {
        auto* bytes=static_cast<std::uint8_t*>(data);
        while(size){
            auto n=sending?send(fd,bytes,size,0):recv(fd,bytes,size,0);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw Error("Host disconnected or attachment timed out. Check th-host and th-client.");
            bytes+=n;
            size-=static_cast<std::size_t>(n);
        }
    }

    SelectionResult submit(std::string_view title,std::string_view token,unsigned port) {
        Socket socket;
        socket.fd=::socket(AF_INET,SOCK_STREAM,0);
        if(socket.fd<0)throw Error("Cannot create picker connection.");
        timeval timeout{65,0};
        setsockopt(socket.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(socket.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        sockaddr_in address{};
        address.sin_family=AF_INET;
        address.sin_port=htons(static_cast<std::uint16_t>(port));
        address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(connect(socket.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)))throw Error("Cannot connect to th-host. Start it first.");
        auto request=hello(selection_request_role,token);
        auto payload=framed(encode_selection(title));
        request.insert(request.end(),payload.begin(),payload.end());
        transfer(socket.fd,request.data(),request.size(),true);
        std::array<std::uint8_t,4> length{};
        transfer(socket.fd,length.data(),length.size(),false);
        auto count=Reader(length).get32();
        if(count>max_frame)throw Error("Invalid host response.");
        Bytes response(count);
        transfer(socket.fd,response.data(),response.size(),false);
        return decode_selection_result(response);
    }
}

int main(int argc,char**argv) {
    std::signal(SIGPIPE,SIG_IGN);
    @autoreleasepool {
        try {
            std::string token_file=".tinyhook-token",title;
            unsigned port=default_agent_port;
            for(int i=1;i<argc;++i){
                std::string option=argv[i];
                if(option=="--help"){
                    std::cout<<"th-pick [--token-file FILE] [--port 9002] [--window TITLE]\n"
                        "Click a game window to attach. Escape or right-click cancels.\n"
                        "Run th-client listen in the game's CrossOver bottle first.\n";
                    return 0;
                }
                if(option!="--token-file"&&option!="--window"&&option!="--port")throw Error("Unknown option: "+option);
                if(++i>=argc)throw Error("Missing value for "+option);
                if(option=="--token-file")token_file=argv[i];
                else if(option=="--window"){
                    title=argv[i];
                    (void)encode_selection(title);
                }
                else {
                    std::string value=argv[i];
                    if(value.empty()||value.size()>5||value.find_first_not_of("0123456789")!=value.npos)throw Error("Invalid port.");
                    port=static_cast<unsigned>(std::stoul(value));
                    if(port<1||port>65535)throw Error("Port must be 1..65535.");
                }
            }
            std::ifstream file(token_file);
            if(!file)throw Error("Cannot read token file: "+token_file);
            std::string token;
            std::getline(file,token);
            if(!token.empty()&&token.back()=='\r')token.pop_back();
            if(!valid_token(token))throw Error("Invalid token file.");
            if(title.empty())title=pick_window();
            if(title.empty())return 2;
            auto result=submit(title,token,port);
            (result.success?std::cout:std::cerr)<<result.message<<'\n';
            return result.success?0:1;
        }
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
}
