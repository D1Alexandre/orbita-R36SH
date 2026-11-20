# Orbita R36S/H — diagnóstico inicial do aparelho

O primeiro passo do port é identificar o hardware e o userspace reais. R36S e
R36H possuem revisões de placa, tela e sistema que não podem ser determinadas
com segurança apenas pelo nome comercial.

O script `tools/r36-probe.sh` é somente leitura, não usa `sudo`, não instala
pacotes e não testa serviços externos. O relatório remove endereços IP, MAC e
formatos comuns de credenciais antes de gravá-los.

## Executar diretamente no aparelho

Copie o script para o console usando o cartão SD ou SSH. Exemplo com SSH:

```sh
scp tools/r36-probe.sh ark@ENDERECO_DO_CONSOLE:/tmp/
ssh ark@ENDERECO_DO_CONSOLE
chmod +x /tmp/r36-probe.sh
cd /tmp
./r36-probe.sh
```

O nome do relatório será semelhante a:

```text
/tmp/r36-probe-20261113T120000Z.txt
```

Copie o relatório de volta para a máquina de desenvolvimento:

```sh
scp ark@ENDERECO_DO_CONSOLE:/tmp/r36-probe-*.txt diagnostics/r36sh/
```

Também é possível indicar o caminho explicitamente:

```sh
./r36-probe.sh --output /tmp/meu-r36h.txt
```

## Log do kernel

Se DRM, GPU ou decoder não forem identificados, execute novamente com o log de
kernel filtrado:

```sh
./r36-probe.sh --include-dmesg --output /tmp/r36-probe-dmesg.txt
```

Algumas imagens Linux bloqueiam `dmesg` para usuários comuns. Nesse caso o
relatório registrará que o comando não estava disponível; não é necessário
executar o diagnóstico inteiro como root.

## O que o relatório decide

| Evidência | Decisão do port |
|---|---|
| SoC, arquitetura e glibc | toolchain e baseline binária |
| Kernel e device tree | backend de vídeo e compatibilidade por revisão |
| DRM, EGL e GLES | SDL KMSDRM ou DRM/GBM/EGL direto |
| MPP, RKVDEC, Hantro e V4L2 | backend H.264 a implementar primeiro |
| ALSA | dispositivo e formato de áudio |
| Input e SDL | mapeamento dos botões e analógicos |
| Interfaces de rede | Wi-Fi, tethering ou Ethernet suportados |
| Temperatura e governors | limites dos testes de desempenho |

Depois de analisar o relatório, o próximo teste será um benchmark controlado de
H.264 720p60. Ele ficará separado do probe porque exige um arquivo de vídeo e
pode consumir CPU, bateria e temperatura do aparelho.

## Privacidade

O filtro reduz informações de rede e credenciais, mas o relatório ainda contém
modelo da placa, versão do sistema, módulos e nomes dos dispositivos. Revise-o
antes de publicá-lo fora da equipe.

