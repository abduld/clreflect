// RUN: %clang_cc1 -triple i386-apple-macosx10.6.6 -fobjc-fragile-abi -emit-llvm -fobjc-exceptions -fcxx-exceptions -fexceptions -o - %s | FileCheck %s
// rdar://8940528

@interface ns_array
+ (id) array;
@end

@implementation ns_array
+ (id) array { return 0; }
@end

id Groups();

@protocol P @end;

@interface INTF<P> {
    double dd;
}
@end

id FUNC() {
    id groups;
    try
    {
      groups = Groups();  // throws on errors.
    }
    catch( INTF<P>* error )
    {
      Groups();
    }
    catch( id error )
    { 
      // CHECK:      landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*)
      // CHECK-NEXT:   catch i8* bitcast ({ i8*, i8*, i32, i8* }* @_ZTIP4INTF to i8*)
      // CHECK-NEXT:   catch i8* bitcast ({ i8*, i8*, i32, i8* }* @_ZTIP11objc_object to i8*)
      // CHECK-NEXT:   catch i8* bitcast ({ i8*, i8*, i32, i8* }* @_ZTIP10objc_class to i8*)
      error = error; 
      groups = [ns_array array]; 
    }
    catch (Class cl) {
      cl = cl;
      groups = [ns_array array];
    }
    return groups;

}

int main() {
  FUNC();
  return 0;
}
