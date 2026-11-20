# Validação inicial — 13/11/2026

O alvo `app/r36s` foi compilado com GCC 9.4 no Ubuntu 20.04 para Linux x86_64
e Linux ARM64. O pacote ARM64 foi construído e seus testes executados em Docker
com emulação da arquitetura. O ELF gerado identifica AArch64 (`e_machine=183`).

Passaram nos dois alvos:

- Armazenamento: criação privada, renovação do token, subdiretórios, remoção,
  rejeição de caminhos externos/symlinks, entropia, cancelamento e relógio monotônico.
- Transporte: 11 cenários de HTTPS local com a implementação libcurl real;
  GET, POST, HTTP 503, redirect não seguido, limite de resposta normal e gzip,
  rejeição de CA não confiável/nome incorreto e cancelamento antes/durante a resposta.
- SDL: desenho offline e tratamento de evento de controle com os drivers dummy.

Os mesmos testes x86_64 também passaram com AddressSanitizer e
UndefinedBehaviorSanitizer. Após o ajuste da saída para controles sem
mapeamento, o teste SDL foi executado novamente com os sanitizers.

O teste de mídia decodificou 30 frames H.264 de uma amostra sintética 1280×720,
com e sem apresentação SDL dummy, no ambiente x86_64. O resultado não mede
CPU, GPU, tela, FPS ou temperatura do R36S/H.

Nos testes de laboratório não foi feito login em uma conta Microsoft real,
nem iniciada uma sessão de jogo. A transmissão WebRTC, o áudio, a aceleração
de vídeo e o mapeamento do controle físico permaneceram pendentes. Na primeira
tentativa de acesso, a porta SSH ainda recusava conexões.

## Validação posterior no aparelho

Com o SSH habilitado e a chave autorizada, o binário ARM64 0.1.0 executou
`--diagnose` no aparelho real. O `ldd` encontrou as dependências do aplicativo
e da ferramenta de vídeo. O sistema observado foi Ubuntu 19.10, glibc 2.30,
kernel 4.4.189 e SDL de runtime 2.30.10. Isso confirma o carregamento do ELF;
ainda não confirma desenho na tela ou login real.

O probe identificou RK3326, modo DSI 640×480p60, controle GO-Super Gamepad,
áudio RK817 e `/dev/vpu_service`. O SDL instalado não oferece o driver dummy:
o smoke test com esse driver retornou `SDL: dummy not available`. A aceleração
H.264 e o desempenho de decodificação no aparelho não foram medidos.

Os testes usaram uma pasta temporária em `/tmp`; nenhum pacote de sistema,
driver ou configuração do frontend foi substituído. Durante as consultas
apareceram erros de leitura de arquivos, `EXT4-fs error` e `Buffer I/O error`.
`/roms` e `/opt/system/Tools` passaram a constar somente para leitura em
`/proc/mounts`, e o `control.txt` do PortMaster não pôde ser lido. Esses erros
impediram a instalação automatizada. A causa física ou lógica não foi determinada.

O usuário posteriormente copiou o pacote manualmente pelo leitor do PC,
sem erro relatado nessa cópia. A cópia bem-sucedida não confirma a integridade
da partição Linux.

## Correção da abertura e logs

Após reiniciar, o PortMaster voltou a ser legível e `/roms` apareceu montado
para gravação. A falha de abertura foi reproduzida: o launcher usava `set -u`,
mas `device_info.txt` do PortMaster consulta `DEVICE_INFO_VERSION` antes de
defini-la. O shell encerrava antes de executar o aplicativo.

O launcher revisão 2 desabilita `nounset` antes de carregar esses helpers e
abre o log antes deles. Ele foi instalado em `/roms/ports/Orbita.sh`, com cópia
do anterior em `/roms/ports/orbita/Orbita.sh.before-launcher-fix`. O diagnóstico
do executável retornou zero depois da correção. Uma abertura pelo frontend
posteriormente registrou `Aguardando o login no celular/PC...`.

O log fica em `/roms/ports/orbita/logs/last-run.log`; se não houver gravação no
cartão, tenta `/dev/shm/orbita-<uid>-logs/last-run.log`. O código Microsoft e os
tokens não são registrados. A fonte também registra as etapas de inicialização
SDL, renderer, controle e apresentação do primeiro frame.

Quatro testes do launcher passaram no Linux x86_64 e ARM64 emulado: helpers com variáveis
opcionais e saída de erro preservada, diagnóstico sem preparar a tela, binário
ausente e log alternativo em RAM. O teste SDL passou novamente após adicionar
os registros de inicialização. As quatro suítes do pacote ARM64 passaram.

O executável com esses registros foi instalado por troca atômica, preservando
o anterior em `orbita/bin/orbita.before-logging-update`. Seus hashes SHA-256
e o do launcher coincidiram com o pacote local. O diagnóstico retornou zero
e identificou os drivers SDL `KMSDRM` e `offscreen`, além do GO-Super Gamepad
com quatro eixos, 17 botões e mapeamento SDL reconhecido.

Durante o uso pelo usuário, o log registrou a autorização Microsoft, renovação
do token, entrada na Xbox Live e no xCloud e obtenção do catálogo: 2.813 títulos
consultados, dois disponíveis à conta, com nomes resolvidos. Outra atualização
reutilizou o token salvo. Depois houve nova solicitação de login sem token;
não foi determinado pelo log se o usuário acionou a saída da conta.

Isso confirma o login e a consulta de catálogo reais no aparelho. A confirmação
visual explícita da tela, o teste completo de botões e a transmissão de jogos
continuam pendentes. Nenhuma sessão de jogo foi iniciada.
