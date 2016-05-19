SECTIONS {
    command_list : {
        __nakd_command_list = .;
        *(.command*);
        LONG(0)
    }
}
INSERT AFTER .text;
