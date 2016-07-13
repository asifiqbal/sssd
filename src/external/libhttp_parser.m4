AC_SUBST(HTTP_PARSER_LIBS)
AC_SUBST(HTTP_PARSER_CFLAGS)

PKG_CHECK_MODULES([HTTP_PARSER], [http_parser], [found_http_parser=yes], [found_http_parser=no])

SSS_AC_EXPAND_LIB_DIR()
AS_IF([test x"$found_http_parser" != xyes],
    [AC_CHECK_HEADERS([http_parser.h],
        [AC_CHECK_LIB([http_parser_strict],
                      [http_parser_init],
                      [HTTP_PARSER_LIBS="-L$sss_extra_libdir -lhttp_parser_strict"],
                      [AC_CHECK_LIB([http_parser],
                                    [http_parser_init],
                                    [HTTP_PARSER_LIBS="-L$sss_extra_libdir -lhttp_parser"],
                                    [AC_MSG_ERROR([libhttp_parser missing http_parser_init])],
                                    [-L$sss_extra_libdir -lhttp_parser])
                      ],
                      [-L$sss_extra_libdir -lhttp_parser_strict])],
        [AC_MSG_WARN([Will use bundled http-parser])])])

AM_CONDITIONAL([BUNDLE_HTTP_PARSER], [test x"$HTTP_PARSER_LIBS" = x])
