/*
 * This code is copyrighted work by Daniel Luz <dev at mernen dot com>.
 *
 * Distributed under the Ruby license: https://www.ruby-lang.org/en/about/license.txt
 */
package json.ext;

import org.jcodings.Encoding;
import org.jcodings.specific.UTF8Encoding;
import org.jruby.Ruby;
import org.jruby.RubyArray;
import org.jruby.RubyBasicObject;
import org.jruby.RubyBignum;
import org.jruby.RubyBoolean;
import org.jruby.RubyFixnum;
import org.jruby.RubyFloat;
import org.jruby.RubyHash;
import org.jruby.RubyIO;
import org.jruby.RubyString;
import org.jruby.runtime.Helpers;
import org.jruby.runtime.ThreadContext;
import org.jruby.runtime.builtin.IRubyObject;
import org.jruby.util.ByteList;
import org.jruby.exceptions.RaiseException;
import org.jruby.util.ConvertBytes;
import org.jruby.util.IOOutputStream;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;

public final class Generator {

    private static final int IO_BUFFER_SIZE = 8192;

    private Generator() {
        throw new RuntimeException();
    }

    /**
     * Encodes the given object as a JSON string, using the given handler.
     */
    static <T extends IRubyObject> RubyString generateJson(ThreadContext context, T object, Handler<? super T> handler) {
        Session session = new Session(null);
        return session.infect(handler.generateNew(context, session, object));
    }

    static <T extends IRubyObject> RubyString generateJson(ThreadContext context, T object, Handler<? super T> handler, IRubyObject arg0) {
        Session session = new Session(arg0);
        return session.infect(handler.generateNew(context, session, object));
    }

    /**
     * Encodes the given object as a JSON string, detecting the appropriate handler
     * for the given object.
     */
    static <T extends IRubyObject> RubyString generateJson(ThreadContext context, T object) {
        Handler<? super T> handler = getHandlerFor(context.runtime, object);
        return generateJson(context, object, handler);
    }

    static <T extends IRubyObject> RubyString generateJson(ThreadContext context, T object, IRubyObject arg0) {
        Handler<? super T> handler = getHandlerFor(context.runtime, object);
        return generateJson(context, object, handler, arg0);
    }

    /**
     * Encodes the given object as a JSON string, using the appropriate
     * handler if one is found or calling #to_json if not.
     */
    public static <T extends IRubyObject> IRubyObject
            generateJson(ThreadContext context, T object,
                         GeneratorState config, IRubyObject io) {
        Session session = new Session(config);
        Handler<? super T> handler = getHandlerFor(context.runtime, object);

        if (io.isNil()) {
            return handler.generateNew(context, session, object);
        }

        BufferedOutputStream buffer = new BufferedOutputStream(new IOOutputStream(io), IO_BUFFER_SIZE);
        handler.generateToBuffer(context, session, object, buffer);
        return io;
    }

    /**
     * Returns the best serialization handler for the given object.
     */
    // Java's generics can't handle this satisfactorily, so I'll just leave
    // the best I could get and ignore the warnings
    @SuppressWarnings("unchecked")
    private static <T extends IRubyObject> Handler<? super T> getHandlerFor(Ruby runtime, T object) {
        switch (((RubyBasicObject) object).getNativeClassIndex()) {
            case NIL    : return (Handler) NIL_HANDLER;
            case TRUE   : return (Handler) TRUE_HANDLER;
            case FALSE  : return (Handler) FALSE_HANDLER;
            case FLOAT  : return (Handler) FLOAT_HANDLER;
            case FIXNUM : return (Handler) FIXNUM_HANDLER;
            case BIGNUM : return (Handler) BIGNUM_HANDLER;
            case STRING :
                if (((RubyBasicObject) object).getMetaClass() != runtime.getString()) break;
                return (Handler) STRING_HANDLER;
            case ARRAY  :
                if (((RubyBasicObject) object).getMetaClass() != runtime.getArray()) break;
                return (Handler) ARRAY_HANDLER;
            case HASH   :
                if (((RubyBasicObject) object).getMetaClass() != runtime.getHash()) break;
                return (Handler) HASH_HANDLER;
        }
        return GENERIC_HANDLER;
    }


    /* Generator context */

    /**
     * A class that concentrates all the information that is shared by
     * generators working on a single session.
     *
     * <p>A session is defined as the process of serializing a single root
     * object; any handler directly called by container handlers (arrays and
     * hashes/objects) shares this object with its caller.
     *
     * <p>Note that anything called indirectly (via {@link #GENERIC_HANDLER})
     * won't be part of the session.
     */
    static class Session {
        private GeneratorState state;
        private IRubyObject possibleState;
        private RuntimeInfo info;
        private StringEncoder stringEncoder;

        private boolean tainted = false;
        private boolean untrusted = false;

        Session(GeneratorState state) {
            this.state = state;
        }

        Session(IRubyObject possibleState) {
            this.possibleState = possibleState == null || possibleState.isNil()
                    ? null : possibleState;
        }

        public GeneratorState getState(ThreadContext context) {
            if (state == null) {
                state = GeneratorState.fromState(context, getInfo(context), possibleState);
            }
            return state;
        }

        public RuntimeInfo getInfo(ThreadContext context) {
            if (info == null) info = RuntimeInfo.forRuntime(context.runtime);
            return info;
        }

        public StringEncoder getStringEncoder(ThreadContext context) {
            if (stringEncoder == null) {
                GeneratorState state = getState(context);
                stringEncoder = new StringEncoder(state.asciiOnly(), state.scriptSafe());
            }
            return stringEncoder;
        }

        public void infectBy(IRubyObject object) {
            if (object.isTaint()) tainted = true;
            if (object.isUntrusted()) untrusted = true;
        }

        public <T extends IRubyObject> T infect(T object) {
            if (tainted) object.setTaint(true);
            if (untrusted) object.setUntrusted(true);
            return object;
        }
    }


    /* Handler base classes */

    private static abstract class Handler<T extends IRubyObject> {
        /**
         * Returns an estimative of how much space the serialization of the
         * given object will take. Used for allocating enough buffer space
         * before invoking other methods.
         */
        int guessSize(ThreadContext context, Session session, T object) {
            return 4;
        }

        RubyString generateNew(ThreadContext context, Session session, T object) {
            ByteListDirectOutputStream buffer = new ByteListDirectOutputStream(guessSize(context, session, object));
            generateToBuffer(context, session, object, buffer);
            return RubyString.newString(context.runtime, buffer.toByteListDirect(UTF8Encoding.INSTANCE));
        }

        void generateToBuffer(ThreadContext context, Session session, T object, OutputStream buffer)  {
            try {
                generate(context, session, object, buffer);
                buffer.flush();
            } catch (IOException ioe) {
                throw context.runtime.newIOErrorFromException(ioe);
            }
        }

        abstract void generate(ThreadContext context, Session session, T object, OutputStream buffer) throws IOException;
    }

    /**
     * A handler that returns a fixed keyword regardless of the passed object.
     */
    private static class KeywordHandler<T extends IRubyObject>
            extends Handler<T> {
        private byte[] keyword;

        private KeywordHandler(String keyword) {
            this.keyword = keyword.getBytes(StandardCharsets.UTF_8);
        }

        @Override
        int guessSize(ThreadContext context, Session session, T object) {
            return keyword.length;
        }

        @Override
        RubyString generateNew(ThreadContext context, Session session, T object) {
            return RubyString.newStringShared(context.runtime, keyword);
        }

        @Override
        void generate(ThreadContext context, Session session, T object, OutputStream buffer) throws IOException {
            buffer.write(keyword);
        }
    }


    /* Handlers */

    static final Handler<RubyBignum> BIGNUM_HANDLER =
        new Handler<RubyBignum>() {
            @Override
            void generate(ThreadContext context, Session session, RubyBignum object, OutputStream buffer) throws IOException {
                BigInteger bigInt = object.getValue();
                buffer.write(bigInt.toString().getBytes(StandardCharsets.UTF_8));
            }
        };

    static final Handler<RubyFixnum> FIXNUM_HANDLER =
        new Handler<RubyFixnum>() {
            @Override
            void generate(ThreadContext context, Session session, RubyFixnum object, OutputStream buffer) throws IOException {
                buffer.write(ConvertBytes.longToCharBytes(object.getLongValue()));
            }
        };

    static final Handler<RubyFloat> FLOAT_HANDLER =
        new Handler<RubyFloat>() {
            @Override
            void generate(ThreadContext context, Session session, RubyFloat object, OutputStream buffer) throws IOException {
                double value = object.getValue();

                if (Double.isInfinite(value) || Double.isNaN(value)) {
                    if (!session.getState(context).allowNaN()) {
                        throw Utils.newException(context, Utils.M_GENERATOR_ERROR, object + " not allowed in JSON");
                    }
                }

                buffer.write(Double.toString(value).getBytes(StandardCharsets.UTF_8));
            }
        };

    private static final byte[] EMPTY_ARRAY_BYTES = "[]".getBytes();
    static final Handler<RubyArray> ARRAY_HANDLER =
        new Handler<RubyArray>() {
            @Override
            int guessSize(ThreadContext context, Session session, RubyArray object) {
                GeneratorState state = session.getState(context);
                int depth = state.getDepth();
                int perItem =
                    4                                           // prealloc
                    + (depth + 1) * state.getIndent().length()  // indent
                    + 1 + state.getArrayNl().length();          // ',' arrayNl
                return 2 + object.size() * perItem;
            }

            @Override
            void generate(ThreadContext context, Session session, RubyArray object, OutputStream buffer) throws IOException {
                GeneratorState state = session.getState(context);
                int depth = state.increaseDepth();

                if (object.isEmpty()) {
                    buffer.write(EMPTY_ARRAY_BYTES);
                    state.decreaseDepth();
                    return;
                }

                Ruby runtime = context.runtime;

                ByteList indentUnit = state.getIndent();
                byte[] shift = Utils.repeat(indentUnit, depth);

                ByteList arrayNl = state.getArrayNl();
                byte[] delim = new byte[1 + arrayNl.length()];
                delim[0] = ',';
                System.arraycopy(arrayNl.unsafeBytes(), arrayNl.begin(), delim, 1,
                        arrayNl.length());

                session.infectBy(object);

                buffer.write((byte)'[');
                buffer.write(arrayNl.bytes());
                boolean firstItem = true;

                for (int i = 0, t = object.getLength(); i < t; i++) {
                    IRubyObject element = object.eltInternal(i);
                    session.infectBy(element);
                    if (firstItem) {
                        firstItem = false;
                    } else {
                        buffer.write(delim);
                    }
                    buffer.write(shift);
                    Handler<IRubyObject> handler = (Handler<IRubyObject>) getHandlerFor(runtime, element);
                    handler.generate(context, session, element, buffer);
                }

                state.decreaseDepth();
                if (arrayNl.length() != 0) {
                    buffer.write(arrayNl.bytes());
                    buffer.write(shift, 0, state.getDepth() * indentUnit.length());
                }

                buffer.write((byte)']');
            }
        };

    private static final byte[] EMPTY_HASH_BYTES = "{}".getBytes();
    static final Handler<RubyHash> HASH_HANDLER =
        new Handler<RubyHash>() {
            @Override
            int guessSize(ThreadContext context, Session session, RubyHash object) {
                GeneratorState state = session.getState(context);
                int perItem =
                    12    // key, colon, comma
                    + (state.getDepth() + 1) * state.getIndent().length()
                    + state.getSpaceBefore().length()
                    + state.getSpace().length();
                return 2 + object.size() * perItem;
            }

            @Override
            void generate(ThreadContext context, final Session session, RubyHash object, final OutputStream buffer) throws IOException {
                final GeneratorState state = session.getState(context);
                final int depth = state.increaseDepth();

                if (object.isEmpty()) {
                    buffer.write(EMPTY_HASH_BYTES);
                    state.decreaseDepth();
                    return;
                }

                final Ruby runtime = context.runtime;

                final ByteList objectNl = state.getObjectNl();
                final byte[] indent = Utils.repeat(state.getIndent(), depth);
                final ByteList spaceBefore = state.getSpaceBefore();
                final ByteList space = state.getSpace();

                buffer.write((byte)'{');
                buffer.write(objectNl.bytes());

                final boolean[] firstPair = new boolean[]{true};
                object.visitAll(new RubyHash.Visitor() {
                    @Override
                    public void visit(IRubyObject key, IRubyObject value) {
                        try {
                            if (firstPair[0]) {
                                firstPair[0] = false;
                            } else {
                                buffer.write((byte) ',');
                                buffer.write(objectNl.bytes());
                            }
                            if (objectNl.length() != 0) buffer.write(indent);

                            IRubyObject keyStr = key.callMethod(context, "to_s");
                            if (keyStr.getMetaClass() == runtime.getString()) {
                                STRING_HANDLER.generate(context, session, (RubyString) keyStr, buffer);
                            } else {
                                Utils.ensureString(keyStr);
                                Handler<IRubyObject> keyHandler = (Handler<IRubyObject>) getHandlerFor(runtime, keyStr);
                                keyHandler.generate(context, session, keyStr, buffer);
                            }
                            session.infectBy(key);

                            buffer.write(spaceBefore.bytes());
                            buffer.write((byte) ':');
                            buffer.write(space.bytes());

                            Handler<IRubyObject> valueHandler = (Handler<IRubyObject>) getHandlerFor(runtime, value);
                            valueHandler.generate(context, session, value, buffer);
                            session.infectBy(value);
                        } catch (Throwable t) {
                            Helpers.throwException(t);
                        }
                    }
                });
                state.decreaseDepth();
                if (!firstPair[0] && objectNl.length() != 0) {
                    buffer.write(objectNl.bytes());
                }
                buffer.write(Utils.repeat(state.getIndent(), state.getDepth()));
                buffer.write((byte)'}');
            }
        };

    static final Handler<RubyString> STRING_HANDLER =
        new Handler<RubyString>() {
            @Override
            int guessSize(ThreadContext context, Session session, RubyString object) {
                // for most applications, most strings will be just a set of
                // printable ASCII characters without any escaping, so let's
                // just allocate enough space for that + the quotes
                return 2 + object.getByteList().length();
            }

            @Override
            void generate(ThreadContext context, Session session, RubyString object, OutputStream buffer) throws IOException {
                RuntimeInfo info = session.getInfo(context);
                RubyString src;

                try {
                    if (object.encoding(context) != info.utf8.get()) {
                        src = (RubyString)object.encode(context, info.utf8.get());
                    } else {
                        src = object;
                    }

                    session.getStringEncoder(context).encode(context, src.getByteList(), buffer);
                } catch (RaiseException re) {
                  throw Utils.newException(context, Utils.M_GENERATOR_ERROR,
                   re.getMessage());
                }
            }
        };

    static final Handler<RubyBoolean> TRUE_HANDLER =
        new KeywordHandler<RubyBoolean>("true");
    static final Handler<RubyBoolean> FALSE_HANDLER =
        new KeywordHandler<RubyBoolean>("false");
    static final Handler<IRubyObject> NIL_HANDLER =
        new KeywordHandler<IRubyObject>("null");

    /**
     * The default handler (<code>Object#to_json</code>): coerces the object
     * to string using <code>#to_s</code>, and serializes that string.
     */
    static final Handler<IRubyObject> OBJECT_HANDLER =
        new Handler<IRubyObject>() {
            @Override
            RubyString generateNew(ThreadContext context, Session session, IRubyObject object) {
                RubyString str = object.asString();
                return STRING_HANDLER.generateNew(context, session, str);
            }

            @Override
            void generate(ThreadContext context, Session session, IRubyObject object, OutputStream buffer) throws IOException {
                RubyString str = object.asString();
                STRING_HANDLER.generate(context, session, str, buffer);
            }
        };

    /**
     * A handler that simply calls <code>#to_json(state)</code> on the
     * given object.
     */
    static final Handler<IRubyObject> GENERIC_HANDLER =
        new Handler<IRubyObject>() {
            @Override
            RubyString generateNew(ThreadContext context, Session session, IRubyObject object) {
                GeneratorState state = session.getState(context);
                if (state.strict()) {
                    throw Utils.newException(context, Utils.M_GENERATOR_ERROR, object + " not allowed in JSON");
                } else if (object.respondsTo("to_json")) {
                    IRubyObject result = object.callMethod(context, "to_json", state);
                    if (result instanceof RubyString) return (RubyString)result;
                    throw context.runtime.newTypeError("to_json must return a String");
                } else {
                    return OBJECT_HANDLER.generateNew(context, session, object);
                }
            }

            @Override
            void generate(ThreadContext context, Session session, IRubyObject object, OutputStream buffer) throws IOException {
                RubyString result = generateNew(context, session, object);
                ByteList bytes = result.getByteList();
                buffer.write(bytes.unsafeBytes(), bytes.begin(), bytes.length());
            }
        };
}
