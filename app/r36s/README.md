# Orbita R36S/H — streaming experimental no ArkOS

Alvo independente do executável PowerPC/XEX do Xbox 360. Usa Linux, SDL2,
libcurl e o núcleo de login Microsoft e catálogo que já existe no projeto.
Não usa conta no site, convite, chave beta, XeLL ou toolchain Xenon.

O alvo 0.2.3 integra sessão xCloud, WebRTC, vídeo H.264 por software,
áudio Opus e envio dos controles. O teste local com um peer Pion valida
essas partes em conjunto. Uma sessão real de HOT WHEELS já entregou frames
720p no R36S, e o usuário confirmou melhora de fluidez após a correção do
decoder. Medições de desempenho, som e resposta de todos os controles
continuam em validação.
Não instale um XEX do Xbox no portátil: são arquiteturas diferentes.

## Compilar no Linux

Dependências de desenvolvimento: compilador C11, CMake 3.16+, pkg-config, SDL2
2.0.10+, libcurl e, para o teste de vídeo, FFmpeg (avformat, avcodec, avutil,
swscale), libopus e as dependências WebRTC estáticas. Os testes também usam
Python 3 e OpenSSL. O script de dependências usa Git e baixa versões fixadas.

```sh
OUTROOT="$PWD/build/webrtc" bash deps/webrtc-linux/build.sh
cmake -S app/r36s -B build/r36s -DCMAKE_BUILD_TYPE=Release \
  -DORBITA_WEBRTC_ROOT="$PWD/build/webrtc/host"
cmake --build build/r36s -j2
(cd build/r36s && ctest --output-on-failure)
cmake --install build/r36s --prefix "$PWD/dist/r36s"
```

Para não gerar a ferramenta de vídeo, use `-DORBITA_BUILD_VIDEO_TEST=OFF`.
Para compilar sem as ferramentas dos testes, use `-DBUILD_TESTING=OFF`.

## Gerar pacote ARM64 com Docker

Na raiz do repositório:

```sh
docker buildx build --platform linux/arm64 -f app/r36s/Dockerfile \
  --target package --output type=local,dest=dist/r36s-arm64 .
```

O ambiente Ubuntu 20.04 mantém a referência de glibc 2.31. Confirme a arquitetura
e a glibc do aparelho com `tools/r36-probe.sh` antes de instalar. Este pacote não
leva bibliotecas gráficas próprias: usa a SDL e os drivers do ArkOS. É necessário
validar as bibliotecas de runtime (`ldd orbita/bin/orbita` e
`ldd orbita/bin/orbita-video-test`) na imagem exata do console. Não substitua a
glibc nem os drivers do aparelho para forçar a execução.

Copie `Orbita.sh` e a pasta `orbita/` para a pasta de ports do cartão ativo:
`/roms/ports/` ou `/roms2/ports/`. Depois abra Orbita pelo EmulationStation.
Se a cópia não preservar os bits de execução, execute no console, a partir
da pasta `ports`: `chmod +x Orbita.sh orbita/bin/orbita orbita/bin/orbita-video-test`.

## Uso e diagnóstico

- O app mostra o endereço e o código Microsoft. Autorize no celular ou PC.
- Cima/baixo: navegar. Esquerda/direita: saltar 6 jogos. L1/R1: saltar 50.
  No teclado, Page Up/Page Down também saltam 50 jogos.
- A: jogar/tentar iniciar. X: buscar jogos. Start: atualizar o catálogo.
- Na busca, use o direcional para escolher letras e A para digitar. Os
  resultados são atualizados enquanto você escreve. X apaga uma letra,
  Y limpa o texto e Start (ou a tecla Buscar) mostra a lista de resultados.
  B cancela a edição e restaura a busca e a posição anteriores.
  Um teclado USB também funciona; Enter mostra os resultados e Backspace apaga.
- A busca ignora maiúsculas, acentos, espaços e pontuação; por exemplo,
  `hotwheels` encontra `HOT WHEELS`. Também aceita IDs do catálogo e da Store.
- Select: localizar Fortnite imediatamente. A tenta iniciar o resultado.
  O atalho depende de o título estar no catálogo retornado pela Microsoft.
- B: limpar a busca e mostrar todos os jogos; na lista completa, sair.
  No computador, X abre a busca, F localiza Fortnite e F5 atualiza o catálogo.
- Na tela de erro, X tenta novamente/atualiza e B volta ao catálogo.
- Y: apagar o login salvo e entrar com outra conta.
- Na fila/conexão, B cancela e volta ao catálogo. Durante o jogo, B é enviado
  ao jogo; segure Select + Start por 0,7 s para encerrar a sessão e voltar.
- Se houver uma sessão do mesmo jogo, A retoma; X encerra a antiga e inicia outra.
- `orbita --diagnose`: versão, arquitetura e bibliotecas, sem login ou rede.
- `orbita --input-test`: identificar botões e analógicos sem fazer login.
- `orbita --windowed`: teste no desktop Linux.
- `orbita --smoke-test`: teste offline da tela SDL e tratamento de evento de controle.
- `orbita --bitrate-kbps 1000`: limite de vídeo (500–8000 kbps; padrão: 1000).
  O padrão foi reduzido em cerca de 17% em relação aos 1200 kbps da versão 0.2.1
  (33% em relação aos 1500 kbps da 0.2.0), para testar menor tráfego. O limite
  é enviado na oferta SDP, nas capacidades do cliente e nos relatórios REMB.
  Continua solicitando 720p e 30 fps; o servidor determina o formato recebido.
  Use `--bitrate-kbps 1200` para comparar com a qualidade anterior.
- `orbita --play-name 'Nome exato do jogo'`: inicia esse título uma vez após
  carregar o catálogo, útil para testar a abertura por SSH.

O menu mantém todos os títulos retornados por `/v2/titles`, inclusive os que
a conta não confirmou poder iniciar. O contador mostra o total e os acessos
confirmados; a linha abaixo da seleção informa a situação daquele jogo.
A tenta iniciar o título selecionado, e o servidor Microsoft decide se a conta
tem acesso. Mostrar um jogo não adiciona assinatura nem licença à conta.
Se a tentativa de início falhar, B volta ao catálogo para escolher outro jogo.

Fortnite tem acesso gratuito pelo Xbox Cloud Gaming, mas o servidor ainda
precisa autorizar a sessão da conta. Na validação de 20/11, o catálogo principal
não confirmou acesso ao Fortnite e o login `xgpuwebf2p` retornou
`OfferingAccessDenied` (403); a tentativa de jogo retornou `NoEntitlement`
(400). Nesse caso, confira se o Fortnite inicia em <https://www.xbox.com/play>
com a mesma conta Microsoft. A busca e o atalho não concedem acesso ao jogo.

Na primeira abertura, a busca de nomes do catálogo completo pode demorar mais.
Os nomes são guardados no cache local; os que faltarem em pt-BR são buscados
em en-US. Se o serviço de nomes falhar repetidamente, o menu abre com os IDs
dos títulos pendentes. X tenta atualizar novamente.

O launcher aproveita o mapeamento SDL do PortMaster, se instalado. Um controle
sem mapeamento ainda precisa ser identificado pelo modo `--input-test`; os
números brutos não são presumidos como iguais em todas as revisões R36S/H.

O log de abertura e os erros do app ficam em `orbita/logs/last-run.log`, inclusive
os erros que acontecem ao carregar o PortMaster antes de iniciar o executável.
Ele registra as etapas da tela e do controle, sem gravar o código Microsoft ou
os tokens. O arquivo é substituído a cada abertura do Orbita.
Durante o streaming, linhas `STAT` a cada dois segundos mostram o estado
WebRTC, frames decodificados, áudio, controles, NACKs e descarte de filas.
No cartão montado no PC: `EASYROMS/ports/orbita/logs/last-run.log`. Por SSH:

```sh
tail -n 80 /roms/ports/orbita/logs/last-run.log
tail -f /roms/ports/orbita/logs/last-run.log
```

Se o cartão não aceitar gravação, o launcher informa um caminho alternativo em
`/dev/shm/orbita-<uid>-logs/last-run.log`; esse log fica em RAM e se perde ao desligar.

O login fica em `${XDG_DATA_HOME:-$HOME/.local/share}/orbita/TOKEN.TXT`,
com diretório 0700, arquivo 0600 e troca atômica ao renovar. Não fica no site.
Use `ORBITA_DATA_DIR` ou `--data-dir` para escolher outro diretório Linux gravável.
A conexão exige os certificados e a hora corretos do sistema. Os testes locais
usam uma CA descartável; a versão instalada sempre verifica CA e nome do servidor.

## H.264 no aparelho

Use um arquivo H.264 local conhecido, de preferência com os parâmetros do stream:

```sh
./orbita/bin/orbita-video-test /caminho/teste.h264 --max-frames 600
./orbita/bin/orbita-video-test /caminho/teste.h264 --show
```

Sem `--show`, o resultado mede decodificação, sem sincronização com a tela. Com
`--show`, testa também apresentação e escala, mantendo a proporção da imagem.
`--decoder NOME` permite selecionar um decoder já disponível na instalação do
FFmpeg; isso não instala nem implementa aceleração Rockchip. O padrão é software.
Nenhum resultado obtido no PC comprova desempenho de streaming no portátil.

## Teste integrado sem conta Microsoft

Compile `app/h264play/hosttest/webrtc/peer_pion` com Go 1.23+ e execute:

```sh
python3 app/r36s/tests/test_stream.py build/r36s/orbita /caminho/peer_pion
```

Esse teste usa FFmpeg CLI para gerar uma imagem sintética. Um peer local
real negocia ICE/DTLS/SRTP/SCTP, entrega H.264 e Opus válidos, e verifica
os relatórios de controle recebidos. O aplicativo apresenta os frames com
SDL dummy e lê pixels da tela para conferir o resultado; não acessa tokens
nem a Microsoft. `-DORBITA_PION_PEER=/caminho/peer_pion` inclui o teste no CTest.

As filas de vídeo e áudio têm limite; vídeo atrasado pede um novo keyframe,
e pacotes de áudio duplicados ou antigos são descartados. Não há aceleração
Rockchip, sincronização audiovisual completa ou garantia de FPS/latência.
O decoder usa três threads de frames/slices. Se o vídeo parar de entregar
frames por cinco segundos durante a sessão, o cliente tenta renegociá-lo
na mesma sessão, seguindo os limites de reconexão da máquina xCloud.

Próximas integrações e evidências: [docs/port-r36sh](../../docs/port-r36sh/README.md).
