<?hh
require_once __DIR__.'/../Framework.php';

class PhpMyAdmin extends Framework {
  public function __construct(string $name) {
    $tc = get_runtime_build().' -v Eval.JitASize=104857600'. // 100MB
          ' -v Eval.JitAStubsSize=104857600'. // 30MB
          ' -v Eval.JitGlobalDataSize=31457280'. // 30MB
          ' '.__DIR__.'/../vendor/bin/phpunit';
    parent::__construct($name, $tc, null, null, true, TestFindModes::TOKEN);
  }
}
