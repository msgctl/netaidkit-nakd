SECTIONS {
    module_list : {
        __nakd_module_list = .;
        *(.module*);
        LONG(0)
    }
}
INSERT AFTER .text;
