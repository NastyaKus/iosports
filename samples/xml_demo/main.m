// iosrt sample: NSXMLParser on an app descriptor-like document: elements,
// attributes, entities, CDATA, comments, namespaces with prefix mapping,
// and a malformed document reported through the error delegate.
#import <Foundation/Foundation.h>

@interface Collector : NSObject <NSXMLParserDelegate>
@property (nonatomic, strong) NSMutableString *log, *text;
@end
@implementation Collector
- (instancetype)init { if ((self = [super init])) { _log = [NSMutableString string]; _text = [NSMutableString string]; } return self; }
- (void)parserDidStartDocument:(NSXMLParser *)p { [self.log appendString:@"start;"]; }
- (void)parserDidEndDocument:(NSXMLParser *)p { [self.log appendString:@"end"]; }
- (void)parser:(NSXMLParser *)p didStartElement:(NSString *)name namespaceURI:(NSString *)uri qualifiedName:(NSString *)q attributes:(NSDictionary *)attrs {
    [self.log appendFormat:@"<%@%@%@>", name, uri ? [NSString stringWithFormat:@" {%@}", uri] : @"", attrs.count ? [NSString stringWithFormat:@" id=%@", attrs[@"id"]] : @""];
    [self.text setString:@""];
}
- (void)parser:(NSXMLParser *)p foundCharacters:(NSString *)s { [self.text appendString:s]; }
- (void)parser:(NSXMLParser *)p foundCDATA:(NSData *)d { [self.text appendString:[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding]]; }
- (void)parser:(NSXMLParser *)p didEndElement:(NSString *)name namespaceURI:(NSString *)uri qualifiedName:(NSString *)q {
    NSString *t = [self.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (t.length) [self.log appendFormat:@"'%@'", t];
    [self.log appendFormat:@"</%@>", name];
    [self.text setString:@""];
}
- (void)parser:(NSXMLParser *)p foundComment:(NSString *)c { [self.log appendString:@"#"]; }
- (void)parser:(NSXMLParser *)p didStartMappingPrefix:(NSString *)prefix toURI:(NSString *)uri { [self.log appendFormat:@"[%@=%@]", prefix, uri]; }
- (void)parser:(NSXMLParser *)p parseErrorOccurred:(NSError *)e { [self.log appendFormat:@"error %ld;", (long)e.code]; }
@end

int main(void) {
    @autoreleasepool {
        NSString *doc = @"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- descriptor -->\n"
            "<application xmlns=\"http://ns.example/air/1.0\" xmlns:x=\"urn:extra\">\n"
            "  <id>com.example.game</id>\n  <name>Gun &amp; Layer &#x2713;</name>\n"
            "  <x:flag id=\"7\"/>\n  <script><![CDATA[a < b && c]]></script>\n</application>";
        NSXMLParser *p = [[NSXMLParser alloc] initWithData:[doc dataUsingEncoding:NSUTF8StringEncoding]];
        Collector *c = [Collector new];
        p.delegate = c; p.shouldProcessNamespaces = YES; p.shouldReportNamespacePrefixes = YES;
        BOOL ok = [p parse];
        NSLog(@"namespaces: ok %d: %@", ok, c.log);

        NSXMLParser *q = [[NSXMLParser alloc] initWithData:[doc dataUsingEncoding:NSUTF8StringEncoding]];
        Collector *d = [Collector new];
        q.delegate = d;
        ok = [q parse];
        NSLog(@"plain: ok %d: %@", ok, d.log);

        NSXMLParser *bad = [[NSXMLParser alloc] initWithData:[@"<a><b></a>" dataUsingEncoding:NSUTF8StringEncoding]];
        Collector *e = [Collector new];
        bad.delegate = e;
        ok = [bad parse];
        NSLog(@"malformed: ok %d: %@ (parserError %d, domain %@)", ok, e.log, bad.parserError != nil, bad.parserError.domain);
    }
    return 0;
}
