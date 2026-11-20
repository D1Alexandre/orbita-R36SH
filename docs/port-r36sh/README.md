# Port Orbita para R36S/H com ArkOS

Objetivo: instalar um executável Linux no portátil, entrar somente com a conta
Microsoft, navegar no catálogo e jogar diretamente pela nuvem. Não depende de
conta no site nem de chave beta.

## Estado do código

| Parte | Estado |
|---|---|
| Alvo Linux independente | `app/r36s/CMakeLists.txt`, sem libxenon ou Xenos |
| Tela | SDL2, canvas 640×480 com fontes e desenho reaproveitados |
| Login e catálogo | Núcleo original, transporte HTTPS com libcurl |
| Credenciais | Diretório local privado, gravação atômica, renovação existente |
| Controles | SDL GameController; modo de identificação para mapeamento por revisão |
| Vídeo H.264 | FFmpeg por software, fila limitada e textura SDL YUV |
| Pacote ArkOS | Launcher EmulationStation/PortMaster e build Docker ARM64 |
| Sessão de jogos/WebRTC | Integrada ao alvo 0.2.0, com signaling e UDP em threads separadas |
| Áudio de streaming | Opus 48 kHz estéreo, fila SDL limitada e recuperação curta de perdas |
| Decoder acelerado Rockchip | Depende do kernel/driver real e de implementação |
| Validação no R36S/H físico | HOT WHEELS transmitiu em 720p; usuário confirmou melhora de fluidez após a correção do decoder |

Evidências do aparelho e limites dos testes em
[`VALIDATION-2026-11-13.md`](VALIDATION-2026-11-13.md) e
[`STREAMING-2026-11-13.md`](STREAMING-2026-11-13.md).

Instruções de build, instalação e comandos em
[`app/r36s/README.md`](../../app/r36s/README.md).

A versão 0.2.3 adiciona busca por nome com teclado na tela, resultados enquanto
digita e o atalho Select para localizar Fortnite. A busca ignora maiúsculas,
acentos, espaços e pontuação, preserva o catálogo e não altera as flags de
acesso. X abre a busca; Start passa a atualizar o catálogo. Detalhes e
validação: [`SEARCH-2026-11-20.md`](SEARCH-2026-11-20.md).

A versão 0.2.2 reduziu o limite padrão de vídeo de 1200 para 1000 kbps,
mantendo o pedido de 720p/30 fps. A melhora nas travadas precisa ser comparada
no aparelho. O pedido aparece no log como `[video] pedido=... bitrate_kbps=...`.
O parâmetro `--bitrate-kbps 1200` permite comparar com a qualidade anterior.

O menu Linux agora mantém o catálogo completo e seus nomes em cache, com
indicação separada de acesso confirmado pela conta. O log do aparelho tinha
2813 títulos recebidos e apenas 2 preservados pelo filtro de acesso. Esse
filtro continua disponível para o fluxo legado; a exibição do catálogo inteiro
não altera as flags de licença nem a autorização de início pelo servidor.
Esquerda/direita saltam 6 títulos; L1/R1 saltam 50.

Detalhes e validação da revisão: [`CATALOG-2026-11-16.md`](CATALOG-2026-11-16.md).

A versão 0.2.2 já foi instalada no aparelho e abriu o catálogo com 2812 títulos,
2811 nomes salvos e 2 acessos confirmados pela conta. A comparação das travadas
com o limite de 1000 kbps depende da próxima partida no portátil.

## Próxima validação no aparelho

1. Conferir som, todos os botões e analógicos durante uma partida.
2. Medir FPS, atraso e temperatura com o streaming completo em uso.
3. Validar recuperação de vídeo, cancelamento de fila, queda de Wi-Fi e encerramento.
4. Avaliar aceleração de vídeo caso a decodificação por software limite o uso.

O alvo Linux usa diretamente a máquina de sessão e o adaptador WebRTC do
projeto, com threads SDL independentes para signaling, UDP e decodificação.
O transporte de sessão usa libcurl com reaproveitamento da conexão TLS.
`deps/webrtc-linux/build.sh` baixa versões fixadas e aplica os patches
green-nx/Orbita; a build Docker inclui essas bibliotecas estáticas.

## Critérios para chamar o port de jogável

- Binário e dependências carregam no ArkOS real sem substituir bibliotecas do sistema.
- Login Microsoft funciona sem conta Orbita e sem fluxo beta.
- Uma sessão real entrega H.264 e Opus, apresenta imagem, reproduz áudio e recebe controles.
- A partida pode ser encerrada e as threads/credenciais liberadas.
- FPS, atraso, perdas e temperatura são medidos no aparelho; nenhum alvo de
  resolução/FPS é garantido antes disso.

A escolha inicial do ambiente Ubuntu 20.04 segue a referência para imagens
ArkOS antigas na [documentação oficial de build do PortMaster](https://portmaster.games/contribute/porting/build-environments/).
O probe do aparelho prevalece sobre essa referência.
