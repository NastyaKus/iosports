// iosrt sample: OpenGL ES through EAGL, the way iOS games set it up:
//  - a UIView whose +layerClass is CAEAGLLayer, an ES 2 context, a
//    framebuffer with a renderbuffer whose storage comes from the layer;
//  - frames driven by CADisplayLink: clear to blue, a scissored red
//    rectangle in the bottom-left corner (GL's origin), present;
//  - an ES 1.1 context with OES framebuffer objects next to it.
// Without a GPU driver (tests, the host) the runtime's null GL stands in:
// clears, scissors and the present path behave, so the frame shows up.
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <OpenGLES/ES1/glext.h>

@interface GLView : UIView
@end
@implementation GLView
+ (Class)layerClass { return [CAEAGLLayer class]; }
@end

@interface GLController : UIViewController
@property (nonatomic, strong) EAGLContext *context;
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic) GLuint fb, rb;
@property (nonatomic) GLint width, height;
@property (nonatomic) int frames;
@end
@implementation GLController
- (void)viewDidLoad {
    [super viewDidLoad];
    GLView *v = [[GLView alloc] initWithFrame:self.view.bounds];
    CAEAGLLayer *layer = (CAEAGLLayer *)v.layer;
    layer.opaque = YES;
    layer.contentsScale = [UIScreen mainScreen].scale;
    layer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking: @NO, kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8 };
    [self.view addSubview:v];

    self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    BOOL cur = [EAGLContext setCurrentContext:self.context];
    NSLog(@"ES2 context: api %lu, current %d (same object %d), layer is CAEAGLLayer %d", (unsigned long)self.context.API, cur,
          [EAGLContext currentContext] == self.context, [layer isKindOfClass:[CAEAGLLayer class]]);
    GLuint fb, rb;
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
    BOOL stored = [self.context renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    GLint w = 0, h = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    self.fb = fb; self.rb = rb; self.width = w; self.height = h;
    NSLog(@"storage from layer %d: %dx%d px (%.0fx scale), framebuffer complete %d, error 0x%x", stored, w, h, layer.contentsScale,
          status == GL_FRAMEBUFFER_COMPLETE, glGetError());
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    NSLog(@"extensions include PVRTC %d, BGRA %d, discard %d", ext && strstr(ext, "GL_IMG_texture_compression_pvrtc") != NULL,
          ext && strstr(ext, "GL_APPLE_texture_format_BGRA8888") != NULL, ext && strstr(ext, "GL_EXT_discard_framebuffer") != NULL);

    /* an ES 1.1 context with OES framebuffer objects */
    EAGLContext *es1 = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES1];
    [EAGLContext setCurrentContext:es1];
    GLuint fb1, rb1; glGenFramebuffersOES(1, &fb1); glBindFramebufferOES(GL_FRAMEBUFFER_OES, fb1);
    glGenRenderbuffersOES(1, &rb1); glBindRenderbufferOES(GL_RENDERBUFFER_OES, rb1);
    glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_RGBA8_OES, 64, 32);
    glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_RENDERBUFFER_OES, rb1);
    glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    GLubyte px[4] = { 0 }; glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    NSLog(@"ES1 context: api %lu, OES framebuffer complete %d, cleared pixel %d,%d,%d,%d", (unsigned long)es1.API,
          glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) == GL_FRAMEBUFFER_COMPLETE_OES, px[0], px[1], px[2], px[3]);
    EAGLContext *shared = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2 sharegroup:self.context.sharegroup];
    NSLog(@"shared context: same sharegroup %d; ES 4 context refused %d", shared.sharegroup == self.context.sharegroup,
          [[EAGLContext alloc] initWithAPI:4] == nil);
    [EAGLContext setCurrentContext:self.context];

    self.link = [CADisplayLink displayLinkWithTarget:self selector:@selector(drawFrame:)];
    [self.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
}
- (void)drawFrame:(CADisplayLink *)link {
    [EAGLContext setCurrentContext:self.context];
    glBindFramebuffer(GL_FRAMEBUFFER, self.fb);
    glViewport(0, 0, self.width, self.height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.1f, 0.2f, 0.8f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 0, self.width / 2, self.height / 4);
    glClearColor(0.9f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBindRenderbuffer(GL_RENDERBUFFER, self.rb);
    BOOL ok = [self.context presentRenderbuffer:GL_RENDERBUFFER];
    if (++self.frames == 3) {
        GLubyte corner[4], top[4];
        glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
        glReadPixels(2, self.height - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
        NSLog(@"frame 3 presented %d: GL bottom-left %d,%d,%d, top %d,%d,%d", ok, corner[0], corner[1], corner[2], top[0], top[1], top[2]);
        [link invalidate];
    }
}
@end

@interface GLAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation GLAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [GLController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([GLAppDelegate class])); }
}
