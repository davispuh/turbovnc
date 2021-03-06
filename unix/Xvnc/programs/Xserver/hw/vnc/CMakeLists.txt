set(DEFAULT_TVNC_USEPAM 1)
option(TVNC_USEPAM "Include PAM authentication support in the TurboVNC Server"
	${DEFAULT_TVNC_USEPAM})

include_directories(. ../../fb ../../mi ../../miext/damage ../../os
	../../randr ../../render ${CMAKE_SOURCE_DIR}/common/rfb)

add_definitions(${ServerOSDefines})
set(PAMSRC "")
if(TVNC_USEPAM)
	add_definitions(-DXVNC_AuthPAM)
	set(PAMSRC authpam.c)
endif()

set(NVCTRLSRC "")
if(TVNC_NVCONTROL)
	set(NVCTRLSRC nvctrlext.c)
	include_directories(${CMAKE_SOURCE_DIR}/unix/libXNVCtrl)
endif()

set(TVNC_USETLS "OpenSSL" CACHE STRING
	"Implement server-side TLS encryption using the specified library [Options: OpenSSL (default), GnuTLS, None]")
set_property(CACHE TVNC_USETLS PROPERTY STRINGS OpenSSL GnuTLS None)
string(TOLOWER "${TVNC_USETLS}" TVNC_USETLS)

if(TVNC_USETLS STREQUAL "openssl")
	include(FindOpenSSL)
	if(NOT OPENSSL_FOUND)
		message(STATUS "OpenSSL not found.  Disabling TLS encryption")
		set(TVNC_USEOPENSSL 0)
	else()
		set(RFBSSLSRC rfbssl_openssl.c)
		message(STATUS "Enabling TLS encryption using OpenSSL")
		add_definitions(-DUSETLS)
		include_directories(${OPENSSL_INCLUDE_DIR})
	endif()
	set(DEFAULT_TVNC_DLOPENSSL 1)
	option(TVNC_DLOPENSSL "Load OpenSSL using dlopen()/dlsym() instead of linking directly with it"
		${DEFAULT_TVNC_DLOPENSSL})
	if(TVNC_DLOPENSSL)
		add_definitions(-DDLOPENSSL)
	endif()
elseif(TVNC_USETLS STREQUAL "gnutls")
	include(FindGnuTLS)
	if(NOT GNUTLS_FOUND)
		message(STATUS "GnuTLS not found.  Disabling TLS encryption")
		set(TVNC_USEGNUTLS 0)
	else()
		set(RFBSSLSRC rfbssl_gnutls.c)
		message(STATUS "Enabling TLS encryption using GnuTLS")
		add_definitions(-DUSETLS ${GNUTLS_DEFINITIONS})
		include_directories(${GNUTLS_INCLUDE_DIR})
	endif()
else()
	message(STATUS "Disabling TLS encryption")
endif()

add_library(vnc STATIC
	auth.c
	cmap.c
	corre.c
	cursor.c
	cutpaste.c
	dispcur.c
	draw.c
	flowcontrol.c
	hextile.c
	httpd.c
	init.c
	input-xkb.c
	kbdptr.c
	randr.c
	rfbserver.c
	rre.c
	sockets.c
	sprite.c
	stats.c
	tight.c
	translate.c
	vncextinit.c
	zlib.c
	zrle.c
	zrleoutstream.c
	zrlepalettehelper.c
	${PAMSRC}
	${NVCTRLSRC}
	${RFBSSLSRC})

if(TVNC_USETLS STREQUAL "openssl" AND NOT TVNC_DLOPENSSL)
	target_link_libraries(vnc ${OPENSSL_LIBRARIES})
elseif(TVNC_USETLS STREQUAL "gnutls")
	target_link_libraries(vnc ${GNUTLS_LIBRARIES})
endif()
