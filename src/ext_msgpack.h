//miaodongdong

#ifndef __EXT_MSGPACK_H__
#define __EXT_MSGPACK_H__
#include "hphp/runtime/base/base-includes.h"
namespace HPHP{

    typedef const String& CStrRef;
    static class MsgpackExtension : public Extension {
    public:
            MsgpackExtension() : Extension("msgpack", HHVM_MSGPACK_VERSION) {}
            void moduleInit();
            void moduleLoad(Hdf config);
            void moduleShutdown();
    } s_msgpack_extension;
}
#endif
