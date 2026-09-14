# Syscalls — planejado

Não há ABI de syscall definida. Ring 3, TSS, processos, spaces virtuais e cópia
segura de buffers user/kernel são pré-requisitos. Futuras APIs devem retornar
erros para ponteiros inválidos e validar handles/comprimentos sem confiar no usuário.
