# Enable handwriting
handwriting:!lipi-toolkit:!t9write {
    include(virtualkeyboard/3rdparty/t9write/t9write-build.pri)
    equals(T9WRITE_FOUND, 1): CONFIG += t9write
    else: CONFIG += lipi-toolkit
}

# Disable built-in layouts
disable-layouts {
    message("The built-in layouts are now excluded from the Qt Virtual Keyboard plugin.")
} else {
    # Enable languages by features
    openwnn: CONFIG += lang-ja_JP
    hangul: CONFIG += lang-ko_KR
    pinyin: CONFIG += lang-zh_CN
    tcime|zhuyin|cangjie: CONFIG += lang-zh_TW

    # Default language
    !contains(CONFIG, lang-.*) {
        contains(QT_CONFIG, private_tests) { # CI or developer build, use all languages
            CONFIG += lang-all
        } else {
            CONFIG += lang-en_GB
        }
    }

    # Flag for activating all languages
    lang-all: CONFIG += \
        lang-ar_AR \
        lang-da_DK \
        lang-de_DE \
        lang-en_GB \
        lang-es_ES \
        lang-fa_FA \
        lang-fi_FI \
        lang-fr_FR \
        lang-hi_IN \
        lang-it_IT \
        lang-ja_JP \
        lang-ko_KR \
        lang-nb_NO \
        lang-pl_PL \
        lang-pt_PT \
        lang-ro_RO \
        lang-ru_RU \
        lang-sv_SE \
        lang-zh_CN \
        lang-zh_TW
}

# Enable features by languages
contains(CONFIG, lang-ja.*)|lang-all: CONFIG += openwnn
contains(CONFIG, lang-ko.*)|lang-all: CONFIG += hangul
contains(CONFIG, lang-zh(_CN)?)|lang-all: CONFIG += pinyin
contains(CONFIG, lang-zh(_TW)?)|lang-all: CONFIG += tcime

# Feature dependencies
tcime {
    !cangjie:!zhuyin: CONFIG += cangjie zhuyin
} else {
    cangjie|zhuyin: CONFIG += tcime
}

# Deprecated configuration flags
disable-xcb {
    message("The disable-xcb option has been deprecated. Please use disable-desktop instead.")
    CONFIG += disable-desktop
}

# Enables logging of sensitive information, such as key events.
# Should never be enabled by default.
sensitive-debug: DEFINES += SENSITIVE_DEBUG
