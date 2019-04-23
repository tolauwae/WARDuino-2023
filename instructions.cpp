#include "instructions.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "debug.h"
#include "mem.h"
#include "util.h"
}

// Size of memory load.
// This starts with the first memory load operator at opcode 0x28
uint32_t LOAD_SIZE[] = {4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4,  // loads
                        4, 8, 4, 8, 1, 2, 1, 2, 4};                // stores

//
// Stack machine (byte code related functions)
//
void push_block(Module *m, Block *block, int sp) {
    m->csp += 1;
    m->callstack[m->csp].block = block;
    m->callstack[m->csp].sp = sp;
    m->callstack[m->csp].fp = m->fp;
    m->callstack[m->csp].ra = m->pc;
}

Block *pop_block(Module *m) {
    Frame *frame = &m->callstack[m->csp--];
    Type *t = frame->block->type;

    // TODO: validate return value if there is one

    m->fp = frame->fp;  // Restore frame pointer

    // Validate the return value
    if (t->result_count == 1) {
        if (m->stack[m->sp].value_type != t->results[0]) {
            sprintf(exception, "call type mismatch");
            return NULL;
        }
    }

    // Restore stack pointer
    if (t->result_count == 1) {
        // Save top value as result
        if (frame->sp < m->sp) {
            m->stack[frame->sp + 1] = m->stack[m->sp];
            m->sp = frame->sp + 1;
        }
    } else {
        if (frame->sp < m->sp) {
            m->sp = frame->sp;
        }
    }

    if (frame->block->block_type == 0x00) {
        // Function, set pc to return address
        m->pc = frame->ra;
    }

    return frame->block;
}

// Setup a function
// Push params and locals on the stack and save a call frame on the call stack
// Sets new pc value for the start of the function
void setup_call(Module *m, uint32_t fidx) {
    Block *func = &m->functions[fidx];
    Type *type = func->type;

    // Push current frame on the call stack
    push_block(m, func, m->sp - type->param_count);

    if (TRACE) {
        dbg_warn("  >> fn0x%x(%d) %s(", fidx, fidx,
             func->export_name ? func->export_name : "");
        for (int p = type->param_count - 1; p >= 0; p--) {
            dbg_warn("%s%s", value_repr(&m->stack[m->sp - p]), p ? " " : "");
        }
        dbg_warn("), %d locals, %d results\n", func->local_count,
             type->result_count);
    }

    // Push locals (dropping extras)
    m->fp = m->sp - type->param_count + 1;
    // TODO: validate arguments vs formal params

    // Push function locals
    for (uint32_t lidx = 0; lidx < func->local_count; lidx++) {
        m->sp += 1;
        m->stack[m->sp].value_type = func->locals[lidx];
        m->stack[m->sp].value.uint64 = 0;  // Initialize whole union to 0
    }

    // Set program counter to start of function
    m->pc = func->start_addr;
    return;
}

/*
   WebAssembly Instructions
   */

/*
   Control Instructions :=
   | nop
   | unreachable
   | block resulttype instr* end
   | loop resulttype instr* end
   | if resulttype instr* else instr* end
   | br labelidx
   | br_if labelidx
   | br_table vec(labelidx) labelidx
   | return
   | call funcidx
   | call_indirect typeidx
   */

/*
Instruction: call funcidx

Webassembly Description:

1. Let 𝐹 be the current frame.
2. Assert: due to validation, 𝐹.module.funcaddrs[𝑥] exists.
3. Let 𝑎 be the function address 𝐹.module.funcaddrs[𝑥].
4. Invoke the function instance at address 𝑎.

Formal specification:

𝐹.module.funcaddrs[𝑥] = 𝑎
-----------------------------------
𝐹; (call 𝑥) -> 𝐹; (invoke 𝑎)
*/

/**
 * 0x02
 */
bool i_instr_block(Module *m, uint32_t *cur_pc) {
    read_LEB(m->bytes, &m->pc, 32);  // ignore block type
    if (m->csp >= CALLSTACK_SIZE) {
        sprintf(exception, "call stack exhausted");
        return false;
    }
    push_block(m, m->block_lookup[*cur_pc], m->sp);
    return true;
}

/**
 * 0x03
 */
bool i_instr_loop(Module *m, uint32_t *cur_pc) {
    read_LEB(m->bytes, &m->pc, 32);  // ignore block type
    if (m->csp >= CALLSTACK_SIZE) {
        sprintf(exception, "call stack exhausted");
        return false;
    }
    push_block(m, m->block_lookup[*cur_pc], m->sp);
    return true;
}

/**
 * 0x04 if
 */
bool i_instr_if(Module *m, uint32_t *cur_pc) {
    read_LEB(m->bytes, &m->pc, 32);  // ignore block type
    Block *block = m->block_lookup[*cur_pc];
    if (m->csp >= CALLSTACK_SIZE) {
        sprintf(exception, "call stack exhausted");
        return false;
    }
    push_block(m, block, m->sp);

    uint32_t cond = m->stack[m->sp--].value.uint32;
    if (cond == 0) {  // if false (I32)
        // branch to else block or after end of if
        if (block->else_addr == 0) {
            // no else block, pop if block and skip end
            m->csp -= 1;
            m->pc = block->br_addr + 1;
        } else {
            m->pc = block->else_addr;
        }
    }
    // if true, keep going
    if (TRACE) {
        debug("      - cond: 0x%x jump to 0x%x, block: %s\n", cond, m->pc,
              block_repr(block));
    }
    return true;
}

/**
 * 0x05 else
 */
bool i_instr_else(Module *m, uint32_t *cur_pc) {
    Block *block = m->callstack[m->csp].block;
    m->pc = block->br_addr;
    if (TRACE) {
        debug("      - of %s jump to 0x%x\n", block_repr(block), m->pc);
    }
    return true;
}

/**
 * 0x0b end
 */
bool i_instr_end(Module *m, uint32_t *cur_pc) {
    Block *block = pop_block(m);
    if (block == NULL) {
        return false;  // an exception (set by pop_block)
    }
    if (TRACE) {
        debug("      - of %s\n", block_repr(block));
    }
    if (block->block_type == 0x00) {  // Function
        if (TRACE) {
            dbg_warn("  << fn0x%x(%d) %s = %s\n", block->fidx, block->fidx,
                 block->export_name ? block->export_name : "",
                 block->type->result_count > 0 ? value_repr(&m->stack[m->sp])
                                               : "_");
        }
        if (m->csp == -1) {
            // Return to top-level
            return true;
        } else {
            // Keep going at return address
        }
    } else if (block->block_type == 0x01) {  // init_expr
        return true;
    } else {  // Block
              // End of block/loop/if, keep going
    }
    return true;
}

/**
 * 0x0c br
 */
bool i_instr_br(Module *m, uint32_t *cur_pc) {
    uint32_t depth = read_LEB(m->bytes, &m->pc, 32);
    m->csp -= depth;
    // set to end for pop_block
    m->pc = m->callstack[m->csp].block->br_addr;
    if (TRACE) {
        debug("      - to: 0x%p\n", &m->pc);
    }
    return true;
}

/**
 * 0x0d br_if
 */
bool i_instr_br_if(Module *m, uint32_t *cur_pc) {
    uint32_t depth = read_LEB(m->bytes, &m->pc, 32);

    uint32_t cond = m->stack[m->sp--].value.uint32;
    if (cond) {  // if true
        m->csp -= depth;
        // set to end for pop_block
        m->pc = m->callstack[m->csp].block->br_addr;
    }
    if (TRACE) {
        debug("      - depth: 0x%x, cond: 0x%x, to: 0x%x\n", depth, cond,
              m->pc);
    }

    return true;
}

/**
 * 0x0e br_table
 */
bool i_instr_br_table(Module *m, uint32_t *cur_pc) {
    uint32_t count = read_LEB(m->bytes, &m->pc, 32);
    if (count > BR_TABLE_SIZE) {
        // TODO: check this prior to runtime
        sprintf(exception, "br_table size %d exceeds max %d\n", count,
                BR_TABLE_SIZE);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        m->br_table[i] = read_LEB(m->bytes, &m->pc, 32);
    }
    uint32_t depth = read_LEB(m->bytes, &m->pc, 32);

    int32_t didx = m->stack[m->sp--].value.int32;
    if (didx >= 0 && didx < (int32_t)count) {
        depth = m->br_table[didx];
    }

    m->csp -= depth;
    // set to end for pop_block
    m->pc = m->callstack[m->csp].block->br_addr;
    if (TRACE) {
        debug("      - count: %d, didx: %d, to: 0x%x\n", count, didx, m->pc);
    }
    return true;
}

/**
 * 0x0f return
 */
bool i_instr_return(Module *m, uint32_t *cur_pc) {
    while (m->csp >= 0 && m->callstack[m->csp].block->block_type != 0x00) {
        m->csp--;
    }
    // Set the program count to the end of the function
    // The actual pop_block and return is handled by the end opcode.
    m->pc = m->callstack[0].block->end_addr;
    if (TRACE) {
        debug("      - to: 0x%x\n", m->pc);
    }
    return true;
}

/**
 * 0x10 call
 */
bool i_instr_call(Module *m, uint32_t *cur_pc) {
    uint32_t fidx = read_LEB(m->bytes, &m->pc, 32);
    if (fidx < m->import_count) {
        ((Primitive)m->functions[fidx].func_ptr)(m);
    } else {
        if (m->csp >= CALLSTACK_SIZE) {
            sprintf(exception, "call stack exhausted");
            return false;
        }
        setup_call(m, fidx);  // regular function call
        if (TRACE) {
            debug("      - calling function fidx: %d at: 0x%x\n", fidx, m->pc);
        }
    }
    return true;
}

/**
 * 0x11 call_indirect
 */
bool i_instr_call_indirect(Module *m, uint32_t *cur_pc) {
    uint32_t tidx = read_LEB(m->bytes, &m->pc, 32);  // TODO: use tidx?
    (void)tidx;
    read_LEB(m->bytes, &m->pc, 1);  // reserved immediate
    uint32_t val = m->stack[m->sp--].value.uint32;
    if (m->options.mangle_table_index) {
        // val is the table address + the index (not sized for the
        // pointer size) so get the actual (sized) index
        if (TRACE) {
            debug("      - entries: %p, original val: 0x%x, new val: 0x%x\n",
                  m->table.entries, val,
                  (uint32_t)((uint64_t)m->table.entries) - val);
        }
        // val = val - (uint32_t)((uint64_t)m->table.entries & 0xFFFFFFFF);
        val = val - (uint32_t)((uint64_t)m->table.entries);
    }
    if (val >= m->table.maximum) {
        sprintf(exception, "undefined element 0x%x (max: 0x%x) in table", val,
                m->table.maximum);
        return false;
    }

    uint32_t fidx = m->table.entries[val];
    if (TRACE) {
        debug("       - call_indirect tidx: %d, val: 0x%x, fidx: 0x%x\n", tidx,
              val, fidx);
    }

    if (fidx < m->import_count) {
        // THUNK thunk_out(m, fidx);    // import/thunk call
    } else {
        Block *func = &m->functions[fidx];
        Type *ftype = func->type;

        if (m->csp >= CALLSTACK_SIZE) {
            sprintf(exception, "call stack exhausted");
            return false;
        }
        if (ftype->mask != m->types[tidx].mask) {
            sprintf(exception,
                    "indirect call type mismatch (call type and function type "
                    "differ)");
            return false;
        }

        setup_call(m, fidx);  // regular function call

        // Validate signatures match
        if ((int)(ftype->param_count + func->local_count) !=
            m->sp - m->fp + 1) {
            sprintf(exception,
                    "indirect call type mismatch (param counts differ)");
            return false;
        }
        for (uint32_t f = 0; f < ftype->param_count; f++) {
            if (ftype->params[f] != m->stack[m->fp + f].value_type) {
                sprintf(exception,
                        "indirect call type mismatch (param types differ)");
                return false;
            }
        }

        if (TRACE) {
            debug(
                "      - tidx: %d, table idx: %d, "
                "calling function fidx: %d at: 0x%x\n",
                tidx, val, fidx, m->pc);
        }
    }
    return true;
}

/**
 * 0x1a drop
 * remvove a value from the stack
 */
bool i_instr_drop(Module *m, uint32_t *cur_pc) {
    m->sp--;
    return true;
}

/**
 * 0x1b select
 *
 * pop the c from the stack,
 * pop val_2 form the stack
 * pop val_1 form the stack
 * if c : push val_1 to the stack
 * else : push val_2 to the stack
 */
bool i_instr_select(Module *m, uint32_t *cur_pc) {
    uint32_t cond = m->stack[m->sp--].value.uint32;
    m->sp--;
    if (!cond) {  // use a instead of b
        m->stack[m->sp] = m->stack[m->sp + 1];
    }
    return true;
}

/**
 * 0x20 get_local
 * move the i-th local to the top of the stack
 */
bool i_instr_get_local(Module *m, uint32_t *cur_pc) {
    int32_t arg = read_LEB(m->bytes, &m->pc, 32);
    if (TRACE) {
        debug("      - arg: 0x%x, got %s\n", arg,
              value_repr(&m->stack[m->fp + arg]));
    }
    m->stack[++m->sp] = m->stack[m->fp + arg];
    return true;
}

/**
 * 0x21 set_local
 */
bool i_instr_set_local(Module *m, uint32_t *cur_pc) {
    int32_t arg = read_LEB(m->bytes, &m->pc, 32);
    m->stack[m->fp + arg] = m->stack[m->sp--];
    if (TRACE) {
        debug("      - arg: 0x%x, to %s\n", arg, value_repr(&m->stack[m->sp]));
    }
    return true;
}

/**
 * 0x0d tee_local
 */
bool i_instr_tee_local(Module *m, uint32_t *cur_pc) {
    int32_t arg = read_LEB(m->bytes, &m->pc, 32);
    m->stack[m->fp + arg] = m->stack[m->sp];
    if (TRACE) {
        debug("      - arg: 0x%x, to %s\n", arg, value_repr(&m->stack[m->sp]));
    }
    return true;
}

/**
 * 0x24 set_global
 */
bool i_instr_set_global(Module *m, uint32_t *cur_pc) {
    uint32_t arg = read_LEB(m->bytes, &m->pc, 32);
    if (TRACE) {
        debug("      - arg: 0x%x, got %s\n", arg, value_repr(&m->globals[arg]));
    }
    m->stack[++m->sp] = m->globals[arg];
    return true;
}

/**
 * 0x3f current_memory
 */
bool i_instr_current_memory(Module *m, uint32_t *cur_pc) {
    read_LEB(m->bytes, &m->pc, 32);  // ignore reserved
    m->stack[++m->sp].value_type = I32;
    m->stack[m->sp].value.uint32 = m->memory.pages;
    return true;
}

/**
 * 0x40 grow_memory
 */
bool i_instr_grow_memory(Module *m, uint32_t *cur_pc) {
    read_LEB(m->bytes, &m->pc, 32);  // ignore reserved
    uint32_t prev_pages = m->memory.pages;
    uint32_t delta = m->stack[m->sp].value.uint32;
    m->stack[m->sp].value.uint32 = prev_pages;
    if (delta == 0) {
        return true;  // No change
    } else if (delta + prev_pages > m->memory.maximum) {
        m->stack[m->sp].value.uint32 = -1;
        return true;
    }
    m->memory.pages += delta;
    m->memory.bytes = (uint8_t *)arecalloc(
        m->memory.bytes, prev_pages * PAGE_SIZE, m->memory.pages * PAGE_SIZE,
        sizeof(uint32_t), "Module->memory.bytes");
    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_mem_load(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    bool overflow = false;
    uint8_t *maddr, *mem_end;
    uint32_t flags = read_LEB(m->bytes, &m->pc, 32);
    uint32_t offset = read_LEB(m->bytes, &m->pc, 32);
    uint32_t addr = m->stack[m->sp--].value.uint32;
    if (flags != 2 && TRACE) {
        dbg_info(
            "      - unaligned load - flags: 0x%x,"
            " offset: 0x%x, addr: 0x%x\n",
            flags, offset, addr);
    }
    if (offset + addr < addr) {
        overflow = true;
    }
    maddr = m->memory.bytes + offset + addr;
    if (maddr < m->memory.bytes) {
        overflow = true;
    }
    mem_end = m->memory.bytes + m->memory.pages * (uint32_t)PAGE_SIZE;
    if (maddr + LOAD_SIZE[opcode - 0x28] > mem_end) {
        overflow = true;
    }
    dbg_info("      - addr: 0x%x, offset: 0x%x, maddr: %p, mem_end: %p\n", addr,
         offset, maddr, mem_end);
    if (!m->options.disable_memory_bounds) {
        if (overflow) {
            dbg_warn("memory start: %p, memory end: %p, maddr: %p\n",
                 m->memory.bytes, mem_end, maddr);
            sprintf(exception, "out of bounds memory access");
            return false;
        }
    }
    m->stack[++m->sp].value.uint64 = 0;  // initialize to 0
    switch (opcode) {
        case 0x28:
            memcpy(&m->stack[m->sp].value, maddr, 4);
            m->stack[m->sp].value_type = I32;
            break;  // i32.load
        case 0x29:
            memcpy(&m->stack[m->sp].value, maddr, 8);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load
        case 0x2a:
            memcpy(&m->stack[m->sp].value, maddr, 4);
            m->stack[m->sp].value_type = F32;
            break;  // f32.load
        case 0x2b:
            memcpy(&m->stack[m->sp].value, maddr, 8);
            m->stack[m->sp].value_type = F64;
            break;  // f64.load
        case 0x2c:
            memcpy(&m->stack[m->sp].value, maddr, 1);
            sext_8_32(&m->stack[m->sp].value.uint32);
            m->stack[m->sp].value_type = I32;
            break;  // i32.load8_s
        case 0x2d:
            memcpy(&m->stack[m->sp].value, maddr, 1);
            m->stack[m->sp].value_type = I32;
            break;  // i32.load8_u
        case 0x2e:
            memcpy(&m->stack[m->sp].value, maddr, 2);
            sext_16_32(&m->stack[m->sp].value.uint32);
            m->stack[m->sp].value_type = I32;
            break;  // i32.load16_s
        case 0x2f:
            memcpy(&m->stack[m->sp].value, maddr, 2);
            m->stack[m->sp].value_type = I32;
            break;  // i32.load16_u
        case 0x30:
            memcpy(&m->stack[m->sp].value, maddr, 1);
            sext_8_64(&m->stack[m->sp].value.uint64);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load8_s
        case 0x31:
            memcpy(&m->stack[m->sp].value, maddr, 1);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load8_u
        case 0x32:
            memcpy(&m->stack[m->sp].value, maddr, 2);
            sext_16_64(&m->stack[m->sp].value.uint64);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load16_s
        case 0x33:
            memcpy(&m->stack[m->sp].value, maddr, 2);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load16_u
        case 0x34:
            memcpy(&m->stack[m->sp].value, maddr, 4);
            sext_32_64(&m->stack[m->sp].value.uint64);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load32_s
        case 0x35:
            memcpy(&m->stack[m->sp].value, maddr, 4);
            m->stack[m->sp].value_type = I64;
            break;  // i64.load32_u
    }
    return true;
}

bool i_instr_mem_store(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint8_t *maddr, *mem_end;
    uint32_t flags = read_LEB(m->bytes, &m->pc, 32);
    uint32_t offset = read_LEB(m->bytes, &m->pc, 32);
    StackValue *sval = &m->stack[m->sp--];
    uint32_t addr = m->stack[m->sp--].value.uint32;
    bool overflow = false;

    if (flags != 2 && TRACE) {
        dbg_info(
            "      - unaligned store - flags: 0x%x,"
            " offset: 0x%x, addr: 0x%x, val: %s\n",
            flags, offset, addr, value_repr(sval));
    }
    if (offset + addr < addr) {
        overflow = true;
    }
    maddr = m->memory.bytes + offset + addr;
    if (maddr < m->memory.bytes) {
        overflow = true;
    }
    mem_end = m->memory.bytes + m->memory.pages * (uint32_t)PAGE_SIZE;
    if (maddr + LOAD_SIZE[opcode - 0x28] > mem_end) {
        overflow = true;
    }
    dbg_info(
        "      - addr: 0x%x, offset: 0x%x, maddr: %p, mem_end: %p, value: "
        "%s\n",
        addr, offset, maddr, mem_end, value_repr(sval));
    if (!m->options.disable_memory_bounds) {
        if (overflow) {
            dbg_warn("memory start: %p, memory end: %p, maddr: %p\n",
                 m->memory.bytes, mem_end, maddr);
            sprintf(exception, "out of bounds memory access");
            return false;
        }
    }
    switch (opcode) {
        case 0x36:
            memcpy(maddr, &sval->value.uint32, 4);
            break;  // i32.store
        case 0x37:
            memcpy(maddr, &sval->value.uint64, 8);
            break;  // i64.store
        case 0x38:
            memcpy(maddr, &sval->value.f32, 4);
            break;  // f32.store
        case 0x39:
            memcpy(maddr, &sval->value.f64, 8);
            break;  // f64.store
        case 0x3a:
            memcpy(maddr, &sval->value.uint32, 1);
            break;  // i32.store8
        case 0x3b:
            memcpy(maddr, &sval->value.uint32, 2);
            break;  // i32.store16
        case 0x3c:
            memcpy(maddr, &sval->value.uint64, 1);
            break;  // i64.store8
        case 0x3d:
            memcpy(maddr, &sval->value.uint64, 2);
            break;  // i64.store16
        case 0x3e:
            memcpy(maddr, &sval->value.uint64, 4);
            break;  // i64.store32
    }
    return true;
}

/**
 * 0x41...0x44 const
 */
bool i_instr_const(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    StackValue *target = &m->stack[++m->sp];

    switch (opcode) {
        case 0x41:  // i32.const
            target->value_type = I32;
            target->value.uint32 = read_LEB_signed(m->bytes, &m->pc, 32);
            break;
        case 0x42:  // i64.const
            target->value_type = I64;
            target->value.int64 = read_LEB_signed(m->bytes, &m->pc, 64);
            break;
        case 0x43:  // f32.const
            target->value_type = F32;
            memcpy(&target->value.uint32, m->bytes + m->pc, 4);
            m->pc += 4;
            break;
        case 0x44:  // f64.const
            target->value_type = F64;
            memcpy(&target->value.uint64, m->bytes + m->pc, 8);
            m->pc += 8;
            break;
    }
    return true;
}

/**
 * 0x45 eqz
 */
bool i_instr_unairy_u32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    switch (opcode) {
        case 0x45:  // i32.eqz
            m->stack[m->sp].value.uint32 = m->stack[m->sp].value.uint32 == 0;
            break;
        case 0x50:  // i64.eqz
            m->stack[m->sp].value_type = I32;
            m->stack[m->sp].value.uint32 = m->stack[m->sp].value.uint64 == 0;
            break;
    }
    return true;
}

/**
 * 0x0d binop32
 */
bool i_instr_math_u32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint32_t a = m->stack[m->sp - 1].value.uint32;
    uint32_t b = m->stack[m->sp].value.uint32;
    uint32_t c;
    m->sp -= 1;
    switch (opcode) {
        case 0x46:
            c = a == b;
            break;  // i32.eq
        case 0x47:
            c = a != b;
            break;  // i32.ne
        case 0x48:
            c = (int32_t)a < (int32_t)b;
            break;  // i32.lt_s
        case 0x49:
            c = a < b;
            break;  // i32.lt_u
        case 0x4a:
            c = (int32_t)a > (int32_t)b;
            break;  // i32.gt_s
        case 0x4b:
            c = a > b;
            break;  // i32.gt_u
        case 0x4c:
            c = (int32_t)a <= (int32_t)b;
            break;  // i32.le_s
        case 0x4d:
            c = a <= b;
            break;  // i32.le_u
        case 0x4e:
            c = (int32_t)a >= (int32_t)b;
            break;  // i32.ge_s
        case 0x4f:
            c = a >= b;
            break;  // i32.ge_u
    }
    m->stack[m->sp].value_type = I32;
    m->stack[m->sp].value.uint32 = c;
    return true;
}

/**
 * 0x0d binop64
 */
bool i_instr_math_u64(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint64_t d = m->stack[m->sp - 1].value.uint64;
    uint64_t e = m->stack[m->sp].value.uint64;
    uint32_t c;
    m->sp -= 1;
    switch (opcode) {
        case 0x51:
            c = d == e;
            break;  // i64.eq
        case 0x52:
            c = d != e;
            break;  // i64.ne
        case 0x53:
            c = (int64_t)d < (int64_t)e;
            break;  // i64.lt_s
        case 0x54:
            c = d < e;
            break;  // i64.lt_u
        case 0x55:
            c = (int64_t)d > (int64_t)e;
            break;  // i64.gt_s
        case 0x56:
            c = d > e;
            break;  // i64.gt_u
        case 0x57:
            c = (int64_t)d <= (int64_t)e;
            break;  // i64.le_s
        case 0x58:
            c = d <= e;
            break;  // i64.le_u
        case 0x59:
            c = (int64_t)d >= (int64_t)e;
            break;  // i64.ge_s
        case 0x5a:
            c = d >= e;
            break;  // i64.ge_u
    }
    m->stack[m->sp].value_type = I32;
    m->stack[m->sp].value.uint32 = c;
    return true;
}

/**
 * 0x0d binop64
 */
bool i_instr_math_f32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    float g = m->stack[m->sp - 1].value.f32;
    float h = m->stack[m->sp].value.f32;
    uint32_t c;
    m->sp -= 1;
    switch (opcode) {
        case 0x5b:
            c = g == h;
            break;  // f32.eq
        case 0x5c:
            c = g != h;
            break;  // f32.ne
        case 0x5d:
            c = g < h;
            break;  // f32.lt
        case 0x5e:
            c = g > h;
            break;  // f32.gt
        case 0x5f:
            c = g <= h;
            break;  // f32.le
        case 0x60:
            c = g >= h;
            break;  // f32.ge
    }
    m->stack[m->sp].value_type = I32;
    m->stack[m->sp].value.uint32 = c;
    return true;
}

/**
 * 0x0d binopf64
 */
bool i_instr_math_f64(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    double j = m->stack[m->sp - 1].value.f64;
    double k = m->stack[m->sp].value.f64;

    uint32_t c;
    m->sp -= 1;
    switch (opcode) {
        case 0x61:
            c = j == k;
            break;  // f64.eq
        case 0x62:
            c = j != k;
            break;  // f64.ne
        case 0x63:
            c = j < k;
            break;  // f64.lt
        case 0x64:
            c = j > k;
            break;  // f64.gt
        case 0x65:
            c = j <= k;
            break;  // f64.le
        case 0x66:
            c = j >= k;
            break;  // f64.ge
    }
    m->stack[m->sp].value_type = I32;
    m->stack[m->sp].value.uint32 = c;
    return true;
}

bool i_instr_unairy_i32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint32_t a = m->stack[m->sp].value.uint32;
    uint32_t c;
    switch (opcode) {
        case 0x67:
            c = a == 0 ? 32 : __builtin_clz(a);
            break;  // i32.clz
        case 0x68:
            c = a == 0 ? 32 : __builtin_ctz(a);
            break;  // i32.ctz
        case 0x69:
            c = __builtin_popcount(a);
            break;  // i32.popcnt
    }
    m->stack[m->sp].value.uint32 = c;
    return true;
}

bool i_instr_unairy_i64(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint64_t d = m->stack[m->sp].value.uint64;
    uint64_t f;
    switch (opcode) {
        case 0x79:
            f = d == 0 ? 64 : __builtin_clzll(d);
            break;  // i64.clz
        case 0x7a:
            f = d == 0 ? 64 : __builtin_ctzll(d);
            break;  // i64.ctz
        case 0x7b:
            f = __builtin_popcountll(d);
            break;  // i64.popcnt
    }
    m->stack[m->sp].value.uint64 = f;
    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_unairy_floating(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    switch (opcode) {
            // unary f32
        case 0x8b:
            m->stack[m->sp].value.f32 = fabs(m->stack[m->sp].value.f32);
            break;  // f32.abs
        case 0x8c:
            m->stack[m->sp].value.f32 = -m->stack[m->sp].value.f32;
            break;  // f32.neg
        case 0x8d:
            m->stack[m->sp].value.f32 = ceil(m->stack[m->sp].value.f32);
            break;  // f32.ceil
        case 0x8e:
            m->stack[m->sp].value.f32 = floor(m->stack[m->sp].value.f32);
            break;  // f32.floor
        case 0x8f:
            m->stack[m->sp].value.f32 = trunc(m->stack[m->sp].value.f32);
            break;  // f32.trunc
        case 0x90:
            m->stack[m->sp].value.f32 = rint(m->stack[m->sp].value.f32);
            break;  // f32.nearest
        case 0x91:
            m->stack[m->sp].value.f32 = sqrt(m->stack[m->sp].value.f32);
            break;  // f32.sqrt

            // unary f64
        case 0x99:
            m->stack[m->sp].value.f64 = fabs(m->stack[m->sp].value.f64);
            break;  // f64.abs
        case 0x9a:
            m->stack[m->sp].value.f64 = -m->stack[m->sp].value.f64;
            break;  // f64.neg
        case 0x9b:
            m->stack[m->sp].value.f64 = ceil(m->stack[m->sp].value.f64);
            break;  // f64.ceil
        case 0x9c:
            m->stack[m->sp].value.f64 = floor(m->stack[m->sp].value.f64);
            break;  // f64.floor
        case 0x9d:
            m->stack[m->sp].value.f64 = trunc(m->stack[m->sp].value.f64);
            break;  // f64.trunc
        case 0x9e:
            m->stack[m->sp].value.f64 = rint(m->stack[m->sp].value.f64);
            break;  // f64.nearest
        case 0x9f:
            m->stack[m->sp].value.f64 = sqrt(m->stack[m->sp].value.f64);
            break;  // f64.sqrt
    }
    return true;
}

/**
 * 0x0d binary_i32
 */
bool i_instr_binary_i32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint32_t a = m->stack[m->sp - 1].value.uint32;
    uint32_t b = m->stack[m->sp].value.uint32;
    uint32_t c;
    m->sp -= 1;
    if (opcode >= 0x6d && opcode <= 0x70 && b == 0) {
        sprintf(exception, "integer divide by zero");
        return false;
    }
    switch (opcode) {
        // case 0x6a: o = __builtin_add_overflow(a, b, &c); break;
        // // i32.add case 0x6b: o = __builtin_sub_overflow(a, b,
        // &c); break;  // i32.sub
        case 0x6a:
            c = a + b;
            break;  // i32.add
        case 0x6b:
            c = a - b;
            break;  // i32.sub
        case 0x6c:
            c = a * b;
            break;  // i32.mul
        case 0x6d:
            if (a == 0x80000000 && b == (uint32_t)-1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            c = (int32_t)a / (int32_t)b;
            break;  // i32.div_s
        case 0x6e:
            c = a / b;
            break;  // i32.div_u
        case 0x6f:
            if (a == 0x80000000 && b == (uint32_t)-1) {
                c = 0;
            } else {
                c = (int32_t)a % (int32_t)b;
            };
            break;  // i32.rem_s
        case 0x70:
            c = a % b;
            break;  // i32.rem_u
        case 0x71:
            c = a & b;
            break;  // i32.and
        case 0x72:
            c = a | b;
            break;  // i32.or
        case 0x73:
            c = a ^ b;
            break;  // i32.xor
        case 0x74:
            c = a << b;
            break;  // i32.shl
        case 0x75:
            c = (int32_t)a >> b;
            break;  // i32.shr_s
        case 0x76:
            c = a >> b;
            break;  // i32.shr_u
        case 0x77:
            c = rotl32(a, b);
            break;  // i32.rotl
        case 0x78:
            c = rotr32(a, b);
            break;  // i32.rotr
    }
    // if (o == 1) {
    //    sprintf(exception, "integer overflow");
    //    return false;
    //}
    m->stack[m->sp].value.uint32 = c;
    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_binary_i64(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    uint64_t d = m->stack[m->sp - 1].value.uint64;
    uint64_t e = m->stack[m->sp].value.uint64;
    uint64_t f;
    m->sp -= 1;
    if (opcode >= 0x7f && opcode <= 0x82 && e == 0) {
        sprintf(exception, "integer divide by zero");
        return false;
    }
    switch (opcode) {
        case 0x7c:
            f = d + e;
            break;  // i64.add
        case 0x7d:
            f = d - e;
            break;  // i64.sub
        case 0x7e:
            f = d * e;
            break;  // i64.mul
        case 0x7f:
            if (d == 0x8000000000000000 && e == (uint32_t)-1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            f = (int64_t)d / (int64_t)e;
            break;  // i64.div_s
        case 0x80:
            f = d / e;
            break;  // i64.div_u
        case 0x81:
            if (d == 0x8000000000000000 && e == (uint32_t)-1) {
                f = 0;
            } else {
                f = (int64_t)d % (int64_t)e;
            }
            break;  // i64.rem_s
        case 0x82:
            f = d % e;
            break;  // i64.rem_u
        case 0x83:
            f = d & e;
            break;  // i64.and
        case 0x84:
            f = d | e;
            break;  // i64.or
        case 0x85:
            f = d ^ e;
            break;  // i64.xor
        case 0x86:
            f = d << e;
            break;  // i64.shl
        case 0x87:
            f = ((int64_t)d) >> e;
            break;  // i64.shr_s
        case 0x88:
            f = d >> e;
            break;  // i64.shr_u
        case 0x89:
            f = rotl64(d, e);
            break;  // i64.rotl
        case 0x8a:
            f = rotr64(d, e);
            break;  // i64.rotr
    }
    m->stack[m->sp].value.uint64 = f;

    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_binary_f32(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    float g = m->stack[m->sp - 1].value.f32;
    float h = m->stack[m->sp].value.f32;
    float i;
    m->sp -= 1;
    switch (opcode) {
        case 0x92:
            i = g + h;
            break;  // f32.add
        case 0x93:
            i = g - h;
            break;  // f32.sub
        case 0x94:
            i = g * h;
            break;  // f32.mul
        case 0x95:
            i = g / h;
            break;  // f32.div
        case 0x96:
            i = wa_fmin(g, h);
            break;  // f32.min
        case 0x97:
            i = wa_fmax(g, h);
            break;  // f32.max
        case 0x98:
            i = signbit(h) ? -fabs(g) : fabs(g);
            break;  // f32.copysign
    }
    m->stack[m->sp].value.f32 = i;
    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_binary_f64(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    double j = m->stack[m->sp - 1].value.f64;
    double k = m->stack[m->sp].value.f64;
    double l;
    m->sp -= 1;
    switch (opcode) {
        case 0xa0:
            l = j + k;
            break;  // f64.add
        case 0xa1:
            l = j - k;
            break;  // f64.sub
        case 0xa2:
            l = j * k;
            break;  // f64.mul
        case 0xa3:
            l = j / k;
            break;  // f64.div
        case 0xa4:
            l = wa_fmin(j, k);
            break;  // f64.min
        case 0xa5:
            l = wa_fmax(j, k);
            break;  // f64.max
        case 0xa6:
            l = signbit(k) ? -fabs(j) : fabs(j);
            break;  // f64.copysign
    }
    m->stack[m->sp].value.f64 = l;

    return true;
}

/**
 * 0x0d XXX
 */
bool i_instr_conversion(Module *m, uint32_t *cur_pc, uint8_t opcode) {
    switch (opcode) {
        case 0xa7:
            m->stack[m->sp].value.uint64 &= 0x00000000ffffffff;
            m->stack[m->sp].value_type = I32;
            break;  // i32.wrap/i64
        case 0xa8:
            if (isnan(m->stack[m->sp].value.f32)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f32 >= INT32_MAX ||
                       m->stack[m->sp].value.f32 < INT32_MIN) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.int32 = m->stack[m->sp].value.f32;
            m->stack[m->sp].value_type = I32;
            break;  // i32.trunc_s/f32
        case 0xa9:
            if (isnan(m->stack[m->sp].value.f32)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f32 >= UINT32_MAX ||
                       m->stack[m->sp].value.f32 <= -1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.uint32 = m->stack[m->sp].value.f32;
            m->stack[m->sp].value_type = I32;
            break;  // i32.trunc_u/f32
        case 0xaa:
            if (isnan(m->stack[m->sp].value.f64)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f64 > INT32_MAX ||
                       m->stack[m->sp].value.f64 < INT32_MIN) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.int32 = m->stack[m->sp].value.f64;
            m->stack[m->sp].value_type = I32;
            break;  // i32.trunc_s/f64
        case 0xab:
            if (isnan(m->stack[m->sp].value.f64)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f64 > UINT32_MAX ||
                       m->stack[m->sp].value.f64 <= -1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.uint32 = m->stack[m->sp].value.f64;
            m->stack[m->sp].value_type = I32;
            break;  // i32.trunc_u/f64
        case 0xac:
            m->stack[m->sp].value.uint64 = m->stack[m->sp].value.uint32;
            sext_32_64(&m->stack[m->sp].value.uint64);
            m->stack[m->sp].value_type = I64;
            break;  // i64.extend_s/i32
        case 0xad:
            m->stack[m->sp].value.uint64 = m->stack[m->sp].value.uint32;
            m->stack[m->sp].value_type = I64;
            break;  // i64.extend_u/i32
        case 0xae:
            if (isnan(m->stack[m->sp].value.f32)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f32 >= INT64_MAX ||
                       m->stack[m->sp].value.f32 < INT64_MIN) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.int64 = m->stack[m->sp].value.f32;
            m->stack[m->sp].value_type = I64;
            break;  // i64.trunc_s/f32
        case 0xaf:
            if (isnan(m->stack[m->sp].value.f32)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f32 >= UINT64_MAX ||
                       m->stack[m->sp].value.f32 <= -1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.uint64 = m->stack[m->sp].value.f32;
            m->stack[m->sp].value_type = I64;
            break;  // i64.trunc_u/f32
        case 0xb0:
            if (isnan(m->stack[m->sp].value.f64)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f64 >= INT64_MAX ||
                       m->stack[m->sp].value.f64 < INT64_MIN) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.int64 = m->stack[m->sp].value.f64;
            m->stack[m->sp].value_type = I64;
            break;  // i64.trunc_s/f64
        case 0xb1:
            if (isnan(m->stack[m->sp].value.f64)) {
                sprintf(exception, "invalid conversion to integer");
                return false;
            } else if (m->stack[m->sp].value.f64 >= UINT64_MAX ||
                       m->stack[m->sp].value.f64 <= -1) {
                sprintf(exception, "integer overflow");
                return false;
            }
            m->stack[m->sp].value.uint64 = m->stack[m->sp].value.f64;
            m->stack[m->sp].value_type = I64;
            break;  // i64.trunc_u/f64
        case 0xb2:
            m->stack[m->sp].value.f32 = m->stack[m->sp].value.int32;
            m->stack[m->sp].value_type = F32;
            break;  // f32.convert_s/i32
        case 0xb3:
            m->stack[m->sp].value.f32 = m->stack[m->sp].value.uint32;
            m->stack[m->sp].value_type = F32;
            break;  // f32.convert_u/i32
        case 0xb4:
            m->stack[m->sp].value.f32 = m->stack[m->sp].value.int64;
            m->stack[m->sp].value_type = F32;
            break;  // f32.convert_s/i64
        case 0xb5:
            m->stack[m->sp].value.f32 = m->stack[m->sp].value.uint64;
            m->stack[m->sp].value_type = F32;
            break;  // f32.convert_u/i64
        case 0xb6:
            m->stack[m->sp].value.f32 = m->stack[m->sp].value.f64;
            m->stack[m->sp].value_type = F32;
            break;  // f32.demote/f64
        case 0xb7:
            m->stack[m->sp].value.f64 = m->stack[m->sp].value.int32;
            m->stack[m->sp].value_type = F64;
            break;  // f64.convert_s/i32
        case 0xb8:
            m->stack[m->sp].value.f64 = m->stack[m->sp].value.uint32;
            m->stack[m->sp].value_type = F64;
            break;  // f64.convert_u/i32
        case 0xb9:
            m->stack[m->sp].value.f64 = m->stack[m->sp].value.int64;
            m->stack[m->sp].value_type = F64;
            break;  // f64.convert_s/i64
        case 0xba:
            m->stack[m->sp].value.f64 = m->stack[m->sp].value.uint64;
            m->stack[m->sp].value_type = F64;
            break;  // f64.convert_u/i64
        case 0xbb:
            m->stack[m->sp].value.f64 = m->stack[m->sp].value.f32;
            m->stack[m->sp].value_type = F64;
            break;  // f64.promote/f32

            // reinterpretations
        case 0xbc:
            m->stack[m->sp].value_type = I32;
            break;  // i32.reinterpret/f32
        case 0xbd:
            m->stack[m->sp].value_type = I64;
            break;  // i64.reinterpret/f64
        case 0xbe:  // memmove(&m->stack[m->sp].value.f32,
                    // &m->stack[m->sp].value.uint32, 4);
            m->stack[m->sp].value_type = F32;
            break;  // f32.reinterpret/i32
        case 0xbf:
            m->stack[m->sp].value_type = F64;
            break;  // f64.reinterpret/i64
    }

    return true;
}