# Instalar o Orbita R36S/H no ArkOS

[English](INSTALL.md) · **Português brasileiro**

Este guia usa o pacote **0.2.3 para Linux ARM64**. Já foi possível jogar via
Xbox Cloud Gaming no R36S/H; o projeto continua experimental, e o desempenho
depende do aparelho, da imagem do ArkOS e da conexão. A versão 0.2.3 adiciona
busca de jogos com teclado na tela.

## O que você precisa

- R36S/H com ArkOS ARM64 e acesso de gravação ao cartão de jogos.
- Internet funcionando no ArkOS, usando um adaptador USB compatível se necessário.
- Conta Microsoft com acesso ao jogo pelo Xbox Cloud Gaming.
- Celular ou computador para autorizar o login Microsoft.
- Computador e leitor de cartão para copiar os arquivos.

O pacote usa as bibliotecas SDL, FFmpeg e Opus do aparelho. Revisões diferentes
do portátil e imagens diferentes do ArkOS podem exigir ajustes. Quando o
PortMaster está disponível, o launcher aproveita seus helpers e mapeamentos.
Não é necessário compilar o projeto, ter conta no site Orbita ou usar chave beta.

## 1. Baixe o pacote certo

Abra as [Releases do projeto](https://github.com/D1Alexandre/orbita-R36SH/releases)
e procure a versão **v0.2.3**. Em **Assets**, baixe:

- `orbita-r36sh-0.2.3.zip`: aplicativo pronto para instalar.
- `orbita-r36sh-0.2.3.zip.sha256`: hash para conferir o download, se desejar.

Use o ZIP do aplicativo. Os arquivos automáticos **Source code (zip/tar.gz)**
contêm o código-fonte e não são o pacote de instalação.

Para conferir o download no PowerShell, abra a pasta onde baixou os arquivos:

```powershell
Get-FileHash .\orbita-r36sh-0.2.3.zip -Algorithm SHA256
Get-Content .\orbita-r36sh-0.2.3.zip.sha256
```

Os hashes devem ser iguais, desconsiderando maiúsculas e minúsculas.

## 2. Copie para o cartão de jogos

1. Desligue o portátil e coloque o cartão de jogos no leitor do computador.
2. Abra a partição de jogos, normalmente chamada `EASYROMS`. Se o Windows pedir
   para formatar uma partição Linux, cancele esse pedido.
3. Extraia o ZIP no computador. Dentro dele estão `Orbita.sh` e a pasta `orbita/`.
4. Copie **os dois** para a pasta `ports` da partição de jogos. Evite criar uma
   pasta extra entre `ports` e `Orbita.sh`.
5. Ejete o cartão com segurança, recoloque-o no portátil e ligue o aparelho.

A estrutura final deve ser:

```text
EASYROMS/
└── ports/
    ├── Orbita.sh
    └── orbita/
        ├── bin/
        │   ├── orbita
        │   └── orbita-video-test
        ├── INSTALL.pt-BR.md
        └── ...
```

No ArkOS, essa pasta normalmente corresponde a `/roms/ports/` ou
`/roms2/ports/`, dependendo do cartão ativo para jogos.

## 3. Abra e faça login

1. Confirme que a internet funciona no ArkOS e que a data e a hora estão corretas.
2. No EmulationStation, abra **Ports** e selecione **Orbita**.
3. O aplicativo mostra um endereço Microsoft e um código de autorização.
4. Acesse o endereço no celular ou PC, entre na sua conta e digite o código.
5. Aguarde o catálogo. Na primeira abertura, a busca dos nomes pode levar mais tempo.
6. Escolha um jogo e pressione **A** para iniciar. A fila depende do serviço.

O login fica salvo localmente para as próximas aberturas. Não compartilhe o
código de autorização nem os arquivos da conta.

## Controles principais

| Ação | Controle |
|---|---|
| Navegar no catálogo | Cima / baixo |
| Saltar 6 jogos | Esquerda / direita |
| Saltar 50 jogos | L1 / R1 |
| Iniciar ou retomar um jogo | A |
| Abrir a busca | X |
| Atualizar o catálogo | Start |
| Localizar Fortnite, se estiver no catálogo | Select |
| Limpar a busca; na lista completa, sair | B |
| Apagar o login salvo e trocar de conta | Y |
| Cancelar a fila ou conexão | B |
| Encerrar a partida e voltar ao catálogo | Segurar Select + Start por 0,7 s |

Na busca, use o direcional para escolher letras e **A** para digitar. **X** apaga
uma letra, **Y** limpa o texto, **Start** mostra os resultados e **B** cancela a
edição. Um teclado USB também funciona. A busca ignora caixa, acentos, espaços
e pontuação: `hotwheels`, por exemplo, encontra `HOT WHEELS`.

Durante a partida, **B** é enviado ao jogo normalmente. Use **Select + Start**
para sair do streaming.

## Atualizar uma instalação existente

Feche o aplicativo e desligue o portátil antes de retirar o cartão. Guarde uma
cópia de `Orbita.sh` e da pasta `orbita/` fora de `ports` para poder voltar à versão
anterior. Copie o launcher e a pasta da nova versão para os mesmos locais.

O login padrão fica no diretório de dados do usuário Linux, separado do pacote;
substituir os arquivos do aplicativo não exige apagá-lo.

## Se não funcionar

| Sintoma | O que conferir |
|---|---|
| Orbita não aparece em Ports | Confira a estrutura acima e se copiou para o cartão ativo de jogos; reinicie o frontend. |
| Abre e fecha imediatamente | Leia `ports/orbita/logs/last-run.log`; verifique arquivos ausentes, permissões e bibliotecas. |
| Login ou conexão falha | Confira internet, data e hora; gere um novo código de login se o anterior expirou. |
| Aparecem IDs em vez de nomes | Aguarde o cache de nomes ou tente atualizar o catálogo; falhas de rede podem atrasar a busca. |
| O jogo aparece, mas retorna `NoEntitlement` | Teste o mesmo jogo em xbox.com/play com a mesma conta; o catálogo não concede acesso ao título. |
| Fortnite não inicia | O atalho só localiza o jogo. A conta testada recebeu `NoEntitlement` / `OfferingAccessDenied`; o início ainda não foi validado. |
| Imagem trava ou tem atraso | Confira a conexão; informe jogo, versão do ArkOS e linhas `STAT` do log para ajudar no diagnóstico. |

Se precisar corrigir permissões, execute no terminal do portátil, dentro da
pasta `ports` correta:

```sh
chmod +x Orbita.sh orbita/bin/orbita orbita/bin/orbita-video-test
```

Para verificar o executável e suas dependências no próprio aparelho:

```sh
./orbita/bin/orbita --diagnose
ldd orbita/bin/orbita
```

Não substitua a glibc nem os drivers do sistema para forçar a execução. Ao relatar
um problema nas [Issues](https://github.com/D1Alexandre/orbita-R36SH/issues),
inclua modelo/revisão, imagem do ArkOS, versão do Orbita, conexão, jogo e passos
para reproduzir. Revise os trechos do log e remova dados da conta antes de publicar.
