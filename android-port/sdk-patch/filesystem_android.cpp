// filesystem_android.cpp - rex::filesystem Android content-URI helpers.
//
// Declared in the SDK's rex/filesystem.h but never implemented upstream.
// On Android, SDL's file dialog returns content:// URIs (Storage Access
// Framework); the asset wizard needs a real file descriptor for anything
// picked that way. Resolved through the activity's ContentResolver via
// JNI (SDL exposes the JNIEnv and activity), detaching the fd from the
// returned ParcelFileDescriptor so plain POSIX reads work.

#include <string>
#include <string_view>

#include <jni.h>

#include <SDL3/SDL.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace rex::filesystem {

void AndroidInitialize() {}
void AndroidShutdown() {}

bool IsAndroidContentUri(std::string_view path) {
  return path.starts_with("content://");
}

int OpenAndroidContentFileDescriptor(const std::string_view uri,
                                     const char* mode) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity) {
    REXLOG_ERROR("Content URI open: SDL Android JNI is not available");
    return -1;
  }
  auto bail = [&]() {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return -1;
  };

  std::string uri_string(uri);
  std::string mode_string(mode);

  jclass uri_class = env->FindClass("android/net/Uri");
  if (!uri_class) return bail();
  jmethodID parse = env->GetStaticMethodID(
      uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
  if (!parse) return bail();
  jstring juri_string = env->NewStringUTF(uri_string.c_str());
  auto juri = static_cast<jobject>(
      env->CallStaticObjectMethod(uri_class, parse, juri_string));
  if (!juri) return bail();

  jclass activity_class = env->GetObjectClass(activity);
  jmethodID get_resolver = env->GetMethodID(
      activity_class, "getContentResolver", "()Landroid/content/ContentResolver;");
  if (!get_resolver) return bail();
  jobject resolver = env->CallObjectMethod(activity, get_resolver);
  if (!resolver) return bail();

  jclass resolver_class = env->GetObjectClass(resolver);
  jmethodID open_fd = env->GetMethodID(
      resolver_class, "openFileDescriptor",
      "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
  if (!open_fd) return bail();
  jstring jmode = env->NewStringUTF(mode_string.c_str());
  jobject pfd = env->CallObjectMethod(resolver, open_fd, juri, jmode);
  if (!pfd) return bail();

  jclass pfd_class = env->GetObjectClass(pfd);
  jmethodID detach_fd = env->GetMethodID(pfd_class, "detachFd", "()I");
  if (!detach_fd) return bail();
  jint fd = env->CallIntMethod(pfd, detach_fd);
  if (env->ExceptionCheck()) return bail();
  return int(fd);
}

}  // namespace rex::filesystem
