/*	
    IFWebController.mm
	Copyright 2001, Apple, Inc. All rights reserved.
*/
#import <WebKit/IFBaseWebController.h>
#import <WebKit/IFWebDataSource.h>
#import <WebKit/IFWebFrame.h>


@interface IFBaseWebControllerPrivate : NSObject
{
    IFWebFrame *mainFrame;
}
@end


@interface IFBaseWebController (IFPrivate);
- (BOOL)_changeLocationTo: (NSURL *)url forFrame: (IFWebFrame *)frame parent: (IFWebDataSource *)parent;
- (void)_changeFrame: (IFWebFrame *)frame dataSource: (IFWebDataSource *)newDataSource;
@end
