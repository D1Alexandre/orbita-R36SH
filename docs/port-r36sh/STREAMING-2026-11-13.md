# Integração de streaming 0.2.0 — 13/11/2026

O catálogo agora permite iniciar um jogo com A. O alvo Linux usa
`xc_session.c`, `xs_machine.c`, `xc_webrtc_xsm.c` e o receptor WebRTC do
projeto. Não usa o binding Xenon de tarefas nem o mecanismo de aluguel de
threads do decoder do Xbox.

Há três workers: signaling HTTPS com libcurl e conexão persistente,
UDP/WebRTC não bloqueante e decodificação H.264 FFmpeg. O loop SDL principal
mantém o renderer e lê o controle. O worker WebRTC recebe uma cópia do estado
do controle e envia os relatórios ao canal `input`.

O áudio usa libopus, 48 kHz estéreo e fila SDL. A fila de vídeo comporta
oito access units; ao exceder o limite, descarta os frames atrasados,
reseta o decoder no próximo IDR e pede keyframe. A fila de áudio é limitada
a aproximadamente 120 ms. Essa recuperação não constitui sincronização
audiovisual completa nem uma medida de atraso total.

## Evidências locais

- As dependências estáticas compilaram em x86_64 e AArch64 emulado, com
  fontes fixadas e os patches green-nx/Orbita. Licenças incluídas no pacote.
- O transporte HTTPS passou em 12 cenários TLS locais. O novo teste faz
  GET/POST/GET na mesma conexão, conferindo uma conexão e dois reusos.
- A máquina de sessão passou em 18.899 checagens existentes, incluindo
  autenticação, fila, renovação, sessões anteriores, timeouts, reconexão e
  encerramento. Os fixtures foram atualizados para o fuso de Brasília e a
  mensagem de fila atual; o corpo JSON foi conferido com o gerador C++.
- Um peer Pion local negociou ICE, DTLS-SRTP, SCTP e os canais xCloud. Em
  sete segundos, o aplicativo decodificou 209 frames e 349 pacotes Opus
  válidos, sem erro de áudio. O SDL apresentou imagem e confirmou pixels
  não pretos. O peer validou relatórios de controle, botões alternados,
  eixos positivos/negativos, sequência, PLI e REMB.
- Esse fluxo também passou com AddressSanitizer e UndefinedBehaviorSanitizer.
  O teste encontrou e permitiu corrigir uma leitura da tela que assumia
  tamanho físico igual ao canvas lógico; a saída fullscreen pode ser maior.

## Evidências do aparelho

O ArkOS possui as bibliotecas com os sonames esperados de FFmpeg e Opus.
Uma amostra sintética H.264 baseline 1280×720, 30 fps e alvo de 1500 kbps,
gerada com x264 ultrafast, teve 60 frames decodificados em 0,259 s pelo
`orbita-video-test` (231,72 fps sem tela). É um teste isolado com conteúdo
simples; não representa uma partida ou um resultado de streaming completo.

As cinco suítes do pacote ARM64 emulado passaram. O executável 0.2.0 foi
instalado por troca atômica em `/roms/ports/orbita/bin/orbita`, preservando
o anterior em `orbita.before-streaming-0.1.0`. O diagnóstico retornou zero
no aparelho real e o loader encontrou FFmpeg, SDL e Opus, sem substituir
bibliotecas do sistema. O arquivo final instalado coincide com o pacote:
`65b4bfd6ca28155b062c3b1e86998518336a49bdc0ec650f7c75abac64b5883a`.

O usuário iniciou HOT WHEELS UNLEASHED 2 pelo frontend. O login renovou,
a criação de sessão foi aceita e o servidor respondeu `WaitingForResources`,
sem `errorDetails`. O usuário confirmou a tela de fila/preparação. A consulta
de espera informou 10 s de provisionamento e zero de alocação, mas a fila
persistiu por minutos; esse valor não foi tratado como prazo garantido.

Depois da fila, a sessão chegou a `Provisioned`, negociou ICE, DTLS e SCTP,
abriu os canais e entregou H.264 Main profile (77), nível 3.2, CABAC,
1280×720/YUV420P. O aplicativo registrou o primeiro frame apresentado.
Os contadores confirmaram milhares de frames decodificados, pacotes Opus
sem erro de decodificação e relatórios de controle enviados ao xCloud.
O usuário confirmou funcionamento com travas/atraso. Não houve confirmação
separada sobre som audível e resposta de cada botão.

O primeiro teste revelou descartes frequentes da fila e períodos sem novos
frames. Também houve erros SRTP e uma re-sinalização. Os contadores não
isolam sozinhos a causa entre carga de decode, fluxo de keyframes e rede.
A instalação atual usa três threads de frames/slices, em vez de somente
slices, e uma fila de oito AUs. Também pede re-sinalização da mesma sessão
quando o vídeo fica cinco segundos sem AUs, mesmo com áudio ainda chegando.
Os logs agora incluem RTP recebido, espera por IDR e pedidos de keyframe.
Essa revisão passou no teste integrado normal com sanitizers e nas cinco
suítes ARM64. Após fechar e reabrir o Orbita e iniciar HOT WHEELS novamente,
o usuário confirmou: “Reabri e o jogo ficou mais fluido”. Isso valida uma
melhora percebida no aparelho. O SSH não respondeu na tentativa de coletar
o novo log; não foi possível quantificar a diferença de FPS ou de descartes.
A recuperação automática após falha ainda precisa de um teste específico.

## Limites atuais

O decoder é por software. Não há binding Rockchip MPP/VPU, relógio comum
de áudio/vídeo ou validação de latência, temperatura e perda de Wi-Fi no
aparelho. O cliente solicita 720p, 30 fps e 1500 kbps; o formato efetivamente
recebido depende da negociação com o servidor e será registrado no log.

Durante conexão/fila, B cancela. No jogo, B é encaminhado normalmente;
Select + Start por 0,7 s encerra a sessão. O DELETE da sessão é feito pelo
worker de signaling antes de liberar as credenciais e terminar os workers.
