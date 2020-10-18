#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "state.h"

/* enable program trace mode */
static bool opt_trace = false;

/* target executable */
static const char *opt_prog_name = "a.out";

static riscv_word_t imp_mem_ifetch(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_ifetch(s->mem, addr);
}

static riscv_word_t imp_mem_read_w(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_w(s->mem, addr);
}

static riscv_half_t imp_mem_read_s(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_s(s->mem, addr);
}

static riscv_byte_t imp_mem_read_b(struct riscv_t *rv, riscv_word_t addr)
{
    state_t *s = rv_userdata(rv);
    return memory_read_b(s->mem, addr);
}

static void imp_mem_write_w(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_word_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

static void imp_mem_write_s(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_half_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

static void imp_mem_write_b(struct riscv_t *rv,
                            riscv_word_t addr,
                            riscv_byte_t data)
{
    state_t *s = rv_userdata(rv);
    memory_write(s->mem, addr, (uint8_t *) &data, sizeof(data));
}

static void imp_on_ecall(struct riscv_t *rv)
{
    syscall_handler(rv);
}

static void imp_on_ebreak(struct riscv_t *rv)
{
    rv_halt(rv);
}

/* run: printing out an instruction trace */
static void run_and_trace(struct riscv_t *rv, elf_t *elf)
{
    const uint32_t cycles_per_step = 1;

    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* trace execution */
        uint32_t pc = rv_get_pc(rv);
        const char *sym = elf_find_symbol(elf, pc);
        printf("%08x  %s\n", pc, (sym ? sym : ""));

        /* step instructions */
        rv_step(rv, cycles_per_step);
    }
}

static void run(struct riscv_t *rv)
{
    const uint32_t cycles_per_step = 100;
    for (; !rv_has_halted(rv);) { /* run until the flag is done */
        /* step instructions */
        rv_step(rv, cycles_per_step);
    }
}

static void print_usage(const char *filename)
{
    fprintf(stderr,
            "RV32I[MA] Emulator which loads an ELF file to execute.\n"
            "Usage: %s [options] [filename]\n"
            "Options:\n"
            "  --trace : print executable trace\n",
            filename);
}

static bool parse_args(int argc, char **args)
{
    /* parse each argument in turn */
    for (int i = 1; i < argc; ++i) {
        const char *arg = args[i];
        /* parse flags */
        if (arg[0] == '-') {
            if (!strcmp(arg, "--help"))
                return false;
            if (!strcmp(arg, "--trace")) {
                opt_trace = true;
                continue;
            }
            /* otherwise, error */
            fprintf(stderr, "Unknown argument '%s'\n", arg);
            return false;
        }
        /* set the executable */
        opt_prog_name = arg;
    }

    return true;
}

int main(int argc, char **args)
{
    if (!parse_args(argc, args)) {
        print_usage(args[0]);
        return 1;
    }

    /* open the ELF file from the file system */
    elf_t *elf = elf_new();
    if (!elf_open(elf, opt_prog_name)) {
        fprintf(stderr, "Unable to open ELF file '%s'\n", opt_prog_name);
        return 1;
    }

    /* install the I/O handlers for the RISC-V runtime */
    const struct riscv_io_t io = {
        imp_mem_ifetch,  imp_mem_read_w,  imp_mem_read_s,
        imp_mem_read_b,  imp_mem_write_w, imp_mem_write_s,
        imp_mem_write_b, imp_on_ecall,    imp_on_ebreak,
    };

    state_t *state = (state_t *) malloc(sizeof(state_t));
    state->mem = memory_new();
    state->break_addr = 0;
    state->fd_map = c_map_init(int, FILE *, c_map_cmp_int);
    {
        FILE *files[] = {stdin, stdout, stderr};
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
            c_map_insert(state->fd_map, &i, &files[i]);
    }

    /* find the start of the heap */
    const struct Elf32_Sym *end;
    if ((end = elf_get_symbol(elf, "_end")))
        state->break_addr = end->st_value;

    /* create the RISC-V runtime */
    struct riscv_t *rv = rv_create(&io, state);
    if (!rv) {
        fprintf(stderr, "Unable to create riscv emulator\n");
        return 1;
    }

    /* load the ELF file into the memory abstraction */
    if (!elf_load(elf, rv, state->mem)) {
        fprintf(stderr, "Unable to load ELF file '%s'\n", args[1]);
        return 1;
    }

    /* run based on the specified mode */
    if (opt_trace) {
        run_and_trace(rv, elf);
    } else {
        run(rv);
    }

    /* finalize the RISC-V runtime */
    elf_delete(elf);
    rv_delete(rv);
    c_map_delete(state->fd_map);
    memory_delete(state->mem);
    free(state);

    return 0;
}
