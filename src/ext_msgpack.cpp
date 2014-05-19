//miaodongdong
#include "ext_msgpack.h"

#include <msgpack.h>

namespace HPHP{
#define MSGPACK_MAX_DEPTH 512
#define HHVM_RCC( name, type, value) \
    Native::registerConstant<type>(\
        makeStaticString(name), \
        value);
#define HHVM_RCC_STRING( name, value) HHVM_RCC( name, KindOfStaticString, makeStaticString(value))
#define HHVM_RCC_LONG( name, value) HHVM_RCC( name, KindOfInt64, value)
#define HHVM_RCC_DOUBLE( name, value) HHVM_RCC( name, KindOfDouble, value)
#define HHVM_RCC_BOOL( name, value) HHVM_RCC( name, KindOfBoolean, value)
    // A holder for a msgpack_zone object; ensures destruction on scope exit
    class MsgpackZone {
    public:

            MsgpackZone(size_t sz = 1024) {
                msgpack_zone_init(&this->_mz, sz);
            }

            ~MsgpackZone() {
                msgpack_zone_destroy(&this->_mz);
            }

            msgpack_zone _mz;
    };

    static bool variant_to_msgpack( CVarRef var,msgpack_object *mo, msgpack_zone *mz, int depth = 0 ){

        if ( MSGPACK_MAX_DEPTH < ++depth) {
            raise_warning("Cowardly refusing to pack object with circular reference");
            return false;
        }
        if (var.isNull()) {
            mo->type = MSGPACK_OBJECT_NIL;
        } else if (var.isBoolean()) {
            mo->type = MSGPACK_OBJECT_BOOLEAN;
            mo->via.boolean = var.toBoolean();
        } else if (var.isDouble()) {
            mo->type = MSGPACK_OBJECT_DOUBLE;
            mo->via.dec = var.toDouble();
        } else if (var.isInteger()) {
             int64_t d = var.toInt64();
            if ( d > 0 ){
                mo->type = MSGPACK_OBJECT_POSITIVE_INTEGER;
                mo->via.u64 = d;
            } else {
                mo->type = MSGPACK_OBJECT_NEGATIVE_INTEGER;
                mo->via.i64 = d;
            }
        } else if (var.isString()) {
            String str = var.toString();
            mo->type = MSGPACK_OBJECT_RAW;
            mo->via.raw.size = static_cast<uint32_t>( str.size() );
            mo->via.raw.ptr = (char*) msgpack_zone_malloc(mz, mo->via.raw.size);
            memcpy((char*)mo->via.raw.ptr, str.data(), str.size());

        } else if (var.isArray()) {
            CArrRef array = var.toCArrRef();
            if( var.getArrayData()->isVectorData()){
                mo->type = MSGPACK_OBJECT_ARRAY;
                mo->via.array.size = array.size();
                mo->via.array.ptr = (msgpack_object*) msgpack_zone_malloc(
                    mz,
                    sizeof(msgpack_object) * mo->via.array.size
                );
                int i = 0;
                for (ArrayIter iter(array); iter; ++iter) {
                    variant_to_msgpack(iter.second(), &mo->via.array.ptr[i], mz, depth);
                    ++ i;
                }

            }else{
                mo->type = MSGPACK_OBJECT_MAP;
                mo->via.map.size = array.size();
                mo->via.map.ptr = (msgpack_object_kv*) msgpack_zone_malloc(
                    mz,
                    sizeof(msgpack_object_kv) * mo->via.map.size
                );

                int i = 0;
                for (ArrayIter iter(array); iter; ++iter) {
                    variant_to_msgpack(iter.first(), &mo->via.map.ptr[i].key, mz, depth);
                    variant_to_msgpack(iter.second(), &mo->via.map.ptr[i].val, mz, depth);
                    ++ i;
                }
            }
        }
        return true;
    }
    static Variant msgpack_to_variant ( msgpack_object *mo )  {
        switch (mo->type) {
            case MSGPACK_OBJECT_NIL:
                return init_null();

            case MSGPACK_OBJECT_BOOLEAN:
                return (mo->via.boolean) ? true : false;

            case MSGPACK_OBJECT_POSITIVE_INTEGER:
                // As per Issue #42, we need to use the base Number
                // class as opposed to the subclass Integer, since
                // only the former takes 64-bit inputs. Using the
                // Integer subclass will truncate 64-bit values.
                return mo->via.u64;

            case MSGPACK_OBJECT_NEGATIVE_INTEGER:
                // See comment for MSGPACK_OBJECT_POSITIVE_INTEGER
                return mo->via.i64;

            case MSGPACK_OBJECT_DOUBLE:
                return mo->via.dec;

            case MSGPACK_OBJECT_ARRAY: {
                                           Array a = Array::Create();
                                           for (uint32_t i = 0; i < mo->via.array.size; i++) {
                                               a.append(msgpack_to_variant(&mo->via.array.ptr[i]));
                                           }
                                           return a;
                                       }

            case MSGPACK_OBJECT_RAW:
                                       if(mo->via.raw.size <= 0 )
                                       {
                                            return String("\0", mo->via.raw.size,CopyString);
                                       }else{
                                            return String(mo->via.raw.ptr, mo->via.raw.size,CopyString);
                                       }

            case MSGPACK_OBJECT_MAP: {
                                         Array array = Array::Create();

                                         for (uint32_t i = 0; i < mo->via.map.size; i++) {
                                             array.set(msgpack_to_variant(&mo->via.map.ptr[i].key),msgpack_to_variant(&mo->via.map.ptr[i].val) );
                                         }

                                         return array;
                                     }

            default:
                                     raise_warning("Encountered unknown MesssagePack object type");
                                     return init_null();
        }
    }


    String f_msgpack_serialize( CVarRef args){
        msgpack_packer pk;
        MsgpackZone mz;
        msgpack_sbuffer * sb = msgpack_sbuffer_new();

        msgpack_packer_init(&pk, sb, msgpack_sbuffer_write);

        msgpack_object mo;

        variant_to_msgpack(args, &mo, &mz._mz, 0);

        if (msgpack_pack_object(&pk, mo)) {
            raise_error("Error serializaing object");
            return false;
        }

        String strval(sb->data,sb->size,CopyString);
        msgpack_sbuffer_free(sb); 
        return strval;

    }
    Variant f_msgpack_unserialize( CStrRef str ){
        if (str.size() < 0 ) {
            raise_warning("size of string less 0");
            return false;
        }

        MsgpackZone mz;
        msgpack_object mo;
        size_t off = 0;

        switch (msgpack_unpack(str.data(), str.size(), &off, &mz._mz, &mo)) {
            case MSGPACK_UNPACK_EXTRA_BYTES:
            case MSGPACK_UNPACK_SUCCESS:
                return msgpack_to_variant(&mo);

            case MSGPACK_UNPACK_CONTINUE:
                raise_warning("MSGPACK_UNPACK_CONTINUE");
                return NULL;

            default:
                raise_warning("Error de-serializing object");
        }
        return false;

    }
    // load all function/class method
    void MsgpackExtension::moduleInit() {
        HHVM_FE(msgpack_serialize);
        HHVM_FE(msgpack_unserialize);
        HHVM_NAMED_FE(msgpack_pack, HHVM_FN(msgpack_serialize));
        HHVM_NAMED_FE(msgpack_unpack, HHVM_FN(msgpack_unserialize));
        loadSystemlib();
    }
    void MsgpackExtension::moduleLoad(Hdf config) {

    }

    void MsgpackExtension::moduleShutdown(){
    }
    HHVM_GET_MODULE(msgpack);
}
